#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <ranges>
#include <vector>
//
#include <glm/glm.hpp>
//
#include <glm/ext.hpp>
//
#include <fmt/format.h>
//
#include <lyrahgames/delaunay/delaunay.hpp>
#include <lyrahgames/gnuplot/gnuplot.hpp>
#include <lyrahgames/pareto/gallery/gallery.hpp>
#include <lyrahgames/pareto/pareto.hpp>

using namespace std;
using namespace fmt;
using namespace lyrahgames;

using real = float;
struct point {
  real x, y, z;
};

int main(int argc, char* argv[]) {
  // Parse command-line arguments.
  if (argc != 6) {
    cerr << "Error: Wrong number of command-line arguments!\n";
    cout << "Usage:\n"
         << argv[0]
         << " <input data file> "
            "<column objective 0> <column objective 1> <column objective 2>"
            "<output file>\n";
    return -1;
  }

  fstream input{argv[1], ios::in};
  if (!input) {
    cerr << "Error: Failed to open '" << argv[1] << "' for reading.\n";
    return -1;
  }

  size_t first, second, third;
  stringstream stream{argv[2]};
  stream >> first;
  stream = stringstream{argv[3]};
  stream >> second;
  stream = stringstream{argv[4]};
  stream >> third;

  fstream output{argv[5], ios::out};
  if (!output) {
    cerr << "Error: Failed to open '" << argv[5] << "' for writing.\n";
    return -1;
  }

  // Read Pareto solutions from input file.
  vector<point> pareto_front{};
  string line;
  while (getline(input, line)) {
    if (line[0] == '#') continue;
    stringstream stream{line};
    point p;
    float tmp;
    size_t i = 0;
    for (; i < first; ++i)
      stream >> tmp;
    stream >> p.x;
    ++i;
    for (; i < second; ++i)
      stream >> tmp;
    stream >> p.y;
    ++i;
    for (; i < third; ++i)
      stream >> tmp;
    stream >> p.z;
    pareto_front.push_back(p);
  }

  // Put Pareto solutions into frontier structure.
  pareto::frontier<real> frontier{pareto_front.size(), 0, 3};
  for (size_t i = 0; i < frontier.sample_count(); ++i) {
    auto it = frontier.objectives_iterator(i);
    *it++ = pareto_front[i].x;
    *it++ = pareto_front[i].y;
    *it = pareto_front[i].z;
  }

  real y[3];
  real max_y[3];
  real min_y[3];

  // Compute AABB of Pareto frontier.
  ranges::copy(frontier.objectives(0), max_y);
  ranges::copy(frontier.objectives(0), min_y);
  for (size_t i = 1; i < frontier.sample_count(); ++i) {
    ranges::copy(frontier.objectives(i), y);
    for (size_t k = 0; k < 3; ++k) {
      max_y[k] = max(max_y[k], y[k]);
      min_y[k] = min(min_y[k], y[k]);
    }
  }

  // Project points on hyperplane for triangulation.
  vector<glm::vec2> uv_points{};
  real pu[3] = {1 / sqrt(2), -1 / sqrt(2), 0};
  real pv[3] = {-1 / sqrt(6), -1 / sqrt(6), 2 / sqrt(6)};
  for (size_t i = 0; i < frontier.sample_count(); ++i) {
    ranges::copy(frontier.objectives(i), y);

    y[0] = (y[0] - min_y[0]) / (max_y[0] - min_y[0]);
    y[1] = (y[1] - min_y[1]) / (max_y[1] - min_y[1]);
    y[2] = (y[2] - min_y[2]) / (max_y[2] - min_y[2]);

    const auto dot_puy = pu[0] * y[0] + pu[1] * y[1] + pu[2] * y[2];
    const auto dot_pvy = pv[0] * y[0] + pv[1] * y[1] + pv[2] * y[2];
    y[0] = dot_puy * pu[0] + dot_pvy * pv[0];
    y[1] = dot_puy * pu[1] + dot_pvy * pv[1];
    y[2] = dot_puy * pu[2] + dot_pvy * pv[2];

    uv_points.push_back({dot_puy, dot_pvy});

    y[0] = y[0] * (max_y[0] - min_y[0]) + min_y[0];
    y[1] = y[1] * (max_y[1] - min_y[1]) + min_y[1];
    y[2] = y[2] * (max_y[2] - min_y[2]) + min_y[2];
  }

  // Triangulate projected points.
  delaunay::triangulator triangulation(uv_points);

  // Estimate connected areas.
  vector<real> distances;
  real mean_distance = 0;
  real p[3], q[3];
  for (auto& e : triangulation.quad_edges) {
    ranges::copy(frontier.objectives(triangulation.permutation[e[0].data]), p);
    ranges::copy(frontier.objectives(triangulation.permutation[e[2].data]), q);
    real distance = 0;
    for (size_t k = 0; k < 3; ++k) {
      const auto t = (p[k] - q[k]) / (max_y[k] - min_y[k]);
      distance += t * t;
    }
    distance = sqrt(distance);
    distances.push_back(distance);
    mean_distance += distance;
  }
  mean_distance /= triangulation.quad_edges.size();

  ranges::sort(distances);
  auto max_allowed_distance = numeric_limits<real>::infinity();
  for (size_t i = 0; i < distances.size() - 1; ++i) {
    if (distances[i] < mean_distance) continue;
    // if (distances[i] / distances[i + 1] > 0.8) continue;
    if (distances[i] / distances[i + 1] > 0.985) continue;
    max_allowed_distance = distances[i];
    std::cout << "index = " << i << '\n'
              << "max allowed dist = " << max_allowed_distance << '\n';
    break;
  }

  // gnuplot::temporary_file delaunay_triangulation{};
  // for (auto& e : triangulation.quad_edges) {
  //   for (int i = 0; i < 4; i += 2) {
  //     const auto p = triangulation.vertices[e[i].data];
  //     y[0] = p[0] * pu[0] + p[1] * pv[0];
  //     y[1] = p[0] * pu[1] + p[1] * pv[1];
  //     y[2] = p[0] * pu[2] + p[1] * pv[2];
  //     y[0] = y[0] * (max_y[0] - min_y[0]) + min_y[0];
  //     y[1] = y[1] * (max_y[1] - min_y[1]) + min_y[1];
  //     y[2] = y[2] * (max_y[2] - min_y[2]) + min_y[2];

  //     ranges::copy(frontier.objectives(triangulation.permutation[e[i].data]),
  //                  y);
  //   }
  // }

  for (size_t i = 0; i < frontier.sample_count(); ++i) {
    output << 'v';
    auto it = frontier.objectives(i);
    for (auto x : frontier.objectives(i))
      output << ' ' << x;
    output << '\n';
  }
  output << '\n';

  for (auto& e : triangulation.quad_edges) {
    ranges::copy(frontier.objectives(triangulation.permutation[e[0].data]), p);
    ranges::copy(frontier.objectives(triangulation.permutation[e[2].data]), q);
    real distance = 0;
    for (size_t k = 0; k < 3; ++k) {
      const auto t = (p[k] - q[k]) / (max_y[k] - min_y[k]);
      distance += t * t;
    }
    distance = sqrt(distance);

    if (distance > max_allowed_distance) continue;
    output << 'l';
    output << ' ' << triangulation.permutation[e[0].data];
    output << ' ' << triangulation.permutation[e[2].data];
    output << '\n';
  }
}
