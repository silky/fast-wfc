#pragma once

#include <unordered_map>
#include <iostream>
#include <limits>
#include <cmath>
#include <random>

#include "matrix.hpp"

using namespace std;

class Wave {
private:
  Matrix<vector<bool>> data;
public:
  Wave() {}

  Wave(unsigned width, unsigned height) : data(width, height) {
  }

  void init(unsigned pattern_size) {
    for(unsigned i = 0; i<get_size(); i++) {
      data.data[i] = vector<bool>(pattern_size, true);
    }
  }

  bool get(unsigned index, unsigned pattern) {
    return data.data[index][pattern];
  }

  void set(unsigned index, unsigned pattern, bool value) {
    data.data[index][pattern] = value;
  }

  bool get(unsigned i, unsigned j, unsigned pattern) {
    return data.get(i,j)[pattern];
  }

  void set(unsigned i, unsigned j, unsigned pattern, bool value) {
    data.get(i,j)[pattern] = value;
  }

  unsigned get_width() {
    return data.width;
  }

  unsigned get_height() {
    return data.height;
  }

  unsigned get_size() {
    return data.data.size();
  }
};

template<typename T>
class WFC {
public:
  std::mt19937 gen;
  std::uniform_real_distribution<> dis;

  Matrix<T> input;
  Matrix<T> output;
  Matrix<unsigned> output_patterns;
  unordered_map<Matrix<T>, unsigned> patterns_frequencies;
  vector<unsigned> patterns_frequencies_by_id;
  vector<double> log_patterns_frequencies_by_id;
  vector<Matrix<T>> patterns;
  Wave wave;
  vector<unsigned> to_propagate;
  vector<bool> is_propagating;
  vector<vector<vector<vector<unsigned>>>> propagator;

  unsigned symmetry;
  bool ground;
  bool periodic_input;
  bool periodic_output;
  unsigned n_width;
  unsigned n_height;

  WFC(const Matrix<T>& input, unsigned out_width, unsigned out_height, unsigned n_width, unsigned n_height,
      unsigned symmetry, bool periodic_input, bool periodic_output, int ground, int seed = 6683)
    : gen(seed), dis(0,1), input(input), output(out_width, out_height),
      symmetry(symmetry), ground(ground),
      periodic_input(periodic_input), periodic_output(periodic_output), n_width(n_width), n_height(n_height)
  {
    unsigned wave_width = periodic_output ? out_width : out_width - n_width + 1;
    unsigned wave_height = periodic_output ? out_height : out_height - n_height + 1;
    wave = Wave(wave_width, wave_height);
    output_patterns = Matrix<unsigned>(wave_width, wave_height);
    is_propagating = vector<bool>(wave_width * wave_height, false);
  }

  bool run() {
    init_patterns();
    init_propagator();
    wave.init(patterns.size());
    init_ground();
    while(true) {
      ObserveStatus result = observe();
      if(result == failure) {
        cout << "failed" << endl;
        return false;
      } else if(result == success) {
        return true;
      }
      propagate();
    }
  }

  unsigned get_id_of_matrix(const Matrix<T>& matrix) {
    for(unsigned i = 0; i<patterns.size(); i++) {
      if(matrix == patterns[i]) {
        return i;
      }
    }
    assert(false);
  }

  void add_pattern(const Matrix<T>& sub_matrix) {
    vector<Matrix<T>> sym(8);
    sym[0] = sub_matrix;
    sym[1] = sym[0].reflected();
    sym[2] = sym[0].rotated();
    sym[3] = sym[2].reflected();
    sym[4] = sym[2].rotated();
    sym[5] = sym[4].reflected();
    sym[6] = sym[4].rotated();
    sym[7] = sym[6].reflected();

    for(unsigned k = 0; k<symmetry; k++) {
      patterns_frequencies[sym[k]] += 1;
      if(patterns_frequencies[sym[k]] == 1) {
        patterns.push_back(sym[k]);
      }
    }
  }

  void init_patterns() {
    Matrix<T> sub_matrix = Matrix<T>(n_width, n_height);
    unsigned max_i = periodic_input ? input.height : input.height - n_height + 1;
    unsigned max_j = periodic_input ? input.width : input.width - n_width + 1;
    for(unsigned i = 0; i < max_i; i++) {
      for(unsigned j = 0; j < max_j; j++) {
        add_pattern(input.get_sub_matrix(i,j,n_width,n_height));
      }
    }

    for (const Matrix<T>& pattern : patterns) {
      patterns_frequencies_by_id.push_back(patterns_frequencies[pattern]);
      log_patterns_frequencies_by_id.push_back(log(patterns_frequencies[pattern]));
    }
  }

  void init_ground() {
    if(!ground) {
      return;
    }
    Matrix<T> ground_matrix = input.get_sub_matrix(input.height - 1, input.width / 2, n_width, n_height);
    unsigned ground_matrix_id = get_id_of_matrix(ground_matrix);

    for(unsigned j = 0; j < wave.get_width(); j++) {
      for(unsigned k = 0; k < patterns.size(); k++) {
        if(ground_matrix_id != k) {
          wave.set(wave.get_height() - 1, j, k, false);
          change(wave.get_height() - 1, j);
        }
      }
    }

    for(unsigned i = 0; i < wave.get_height() - 1; i++) {
      for(unsigned j = 0; j < wave.get_width(); j++) {
        wave.set(i, j, ground_matrix_id, false);
        change(i,j);
      }
    }

    propagate();
  }

  void init_propagator() {
    propagator = vector<vector<vector<vector<unsigned>>>>(2 * n_width - 1);
    for(unsigned x = 0; x < 2 * n_width - 1; x++) {
      propagator[x] = vector<vector<vector<unsigned>>>(2 * n_height - 1);
      for(unsigned y = 0; y < 2 * n_height - 1; y++) {
        propagator[x][y] = vector<vector<unsigned>>(patterns.size());
        for(unsigned k1 = 0; k1 < patterns.size(); k1++) {
          propagator[x][y][k1] = vector<unsigned>();
          for(unsigned k2 = 0; k2 < patterns.size(); k2++) {
            if(agrees(patterns[k1], patterns[k2], x - n_width + 1, y - n_height + 1)) {
              propagator[x][y][k1].push_back(k2);
            }
          }
        }
      }
    }
  }

  bool agrees(Matrix<T> pattern1, Matrix<T> pattern2, int dx, int dy) {
    unsigned xmin = dx < 0 ? 0 : dx;
    unsigned xmax = dx < 0 ? dx + n_width : n_width;
    unsigned ymin = dy < 0 ? 0 : dy;
    unsigned ymax = dy < 0 ? dy + n_height : n_height;
    for(unsigned y = ymin; y < ymax; y++) {
      for(unsigned x = xmin; x < xmax; x++) {
        if(pattern1.get(y,x) != pattern2.get(y-dy,x-dx)) {
          return false;
        }
      }
    }
    return true;
  }

  void wave_to_output() {
    for(unsigned i = 0; i< wave.get_size(); i++) {
      for(unsigned k = 0; k < patterns.size(); k++) {
        if(wave.get(i, k)) {
          output_patterns.data[i] = k;
        }
      }
    }

    if(periodic_output) {
      for(unsigned y = 0; y < wave.get_height(); y++) {
        for(unsigned x = 0; x < wave.get_width(); x++) {
          output.get(y,x) = patterns[output_patterns.get(y,x)].get(0,0);
        }
      }
    } else {
      for(unsigned y = 0; y < wave.get_height(); y++) {
        for(unsigned x = 0; x < wave.get_width(); x++) {
          for(unsigned dy = 0; dy < n_height; dy++) {
            for(unsigned dx = 0; dx < n_width; dx++) {
              output.get(y + dy, x + dx) = patterns[output_patterns.get(y,x)].get(dy,dx);
            }
          }
        }
      }
    }
  }

  int get_min_entropy() {
    double min = std::numeric_limits<float>::infinity();
    int argmin = -1;
    for(unsigned i = 0; i < wave.get_size(); i++) {
      double sum = 0;
      int nb_possibilities = 0;
      for(unsigned k = 0; k < patterns.size(); k++) {
        if(wave.get(i,k)) {
          sum += patterns_frequencies_by_id[k];
          nb_possibilities++;
        }
      }

      if(sum == 0) {
        return -2;
      }

      double noise = dis(gen) * 1e-5;
      double entropy;
      double main_sum = 0;
      double log_sum = log(sum);

      if(nb_possibilities == 1) {
        entropy = 0;
      } else {
        for(unsigned k = 0; k<patterns.size(); k++) {
          if(wave.get(i,k)) {
            main_sum += patterns_frequencies_by_id[k] * log_patterns_frequencies_by_id[k];
          }
        }
        entropy = log_sum - main_sum / sum;
      }

      if(entropy > 0 && entropy + noise < min) {
        min = entropy + noise;
        argmin = i;
      }
    }
    return argmin;
  }

  enum ObserveStatus {
    success,
    failure,
    to_continue
  };

  ObserveStatus observe() {
    int argmin = get_min_entropy();
    if(argmin == -2) {
      return failure;
    }

    if(argmin == -1) {
      wave_to_output();
      return success;
    }

    double s = 0;
    double random_value = dis(gen);
    unsigned chosen_value;

    for(unsigned k = 0; k < patterns.size(); k++) {
      s+= wave.get(argmin,k) ? patterns_frequencies_by_id[k] : 0;
    }
    random_value *= s;

    for(unsigned k = 0; k < patterns.size(); k++) {
      random_value -= wave.get(argmin,k) ? patterns_frequencies_by_id[k] : 0;
      if(random_value <= 0 || k == patterns.size() - 1) {
        chosen_value = k;
        break;
      }
    }

    for(unsigned k = 0; k < patterns.size(); k++) {
      wave.set(argmin, k, k == chosen_value);
    }

    change(argmin);

    return to_continue;
  }

  void propagate() {
    while(to_propagate.size() != 0) {
      unsigned i1 = to_propagate.back();
      to_propagate.pop_back();
      is_propagating[i1] = false;

      unsigned x1 = i1 % wave.get_width();
      unsigned y1 = i1 / wave.get_width();

      for(int dx = -int(n_width) + 1; dx < int(n_width); dx++) {
        for(int dy = -int(n_height) + 1; dy < int(n_height); dy++) {
          int x2, y2;
          if(periodic_output) {
            x2 = ((int)x1 + dx + (int)wave.get_width()) % wave.get_width();
            y2 = ((int)y1 + dy + (int)wave.get_height()) % wave.get_height();
          } else {
            x2 = x1 + dx;
            y2 = y1 + dy;
            if(x2 < 0 || x2 >= (int)wave.get_width()) {
              continue;
            }
            if(y2 < 0 || y2 >= (int)wave.get_height()) {
              continue;
            }
          }

          unsigned i2 = x2 + y2 * wave.get_width();
          const vector<vector<unsigned>>& prop = propagator[n_width - 1 - dx][n_height - 1 - dy];
          for(unsigned k2 = 0; k2 < patterns.size(); k2++) {
            if(wave.get(i2, k2)) {
              bool b = false;
              for(unsigned pattern : prop[k2]) {
                b = wave.get(i1, pattern);
                if(b) {
                  break;
                }
              }
              if(!b) {
                change(i2);
                wave.set(i2, k2, false);
              }
            }
          }
        }
      }
    }
  }

  void change(unsigned i) {
    if(is_propagating[i]) {
      return;
    }
    to_propagate.push_back(i);
    is_propagating[i] = true;
  }

  void change(unsigned i, unsigned j) {
    change(j + wave.get_height() * i);
  }
};

