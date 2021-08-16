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

int main(int argc, char *argv[]) {
  mt19937 rng{random_device{}()};
  normal_distribution<real> distribution{0, 1};
  const auto random = [&] { return distribution(rng); };

  auto problem = pareto::gallery::pawellek<real>;
  const auto frontier = pareto::naive::optimization<pareto::frontier<real>>(
      problem, rng, 8'000'000);
  // const auto frontier = pareto::nsga2::optimization<pareto::frontier<real>>(
  //     problem, rng, {.iterations = 1000, .population = 1000});

  real x[problem.parameter_count()];
  real y[3];
  real max_y[3];
  real min_y[3];

  ranges::copy(frontier.objectives(0), max_y);
  ranges::copy(frontier.objectives(0), min_y);
  for (size_t i = 1; i < frontier.sample_count(); ++i) {
    ranges::copy(frontier.objectives(i), y);
    for (size_t k = 0; k < 3; ++k) {
      max_y[k] = max(max_y[k], y[k]);
      min_y[k] = min(min_y[k], y[k]);
    }
  }

  gnuplot::temporary_file feasible_solutions{};
  const size_t n = 5;
  for (size_t i = 0; i < frontier.sample_count(); ++i) {
    for (size_t j = 0; j < n; ++j) {
      ranges::copy(frontier.parameters(i), x);
      for (size_t k = 0; k < problem.parameter_count(); ++k) {
        x[k] += random() * 0.5 * (j + 1) / n;
        x[k] = clamp(x[k], problem.box_min(k), problem.box_max(k));
      }
      problem.evaluate(x, y);
      feasible_solutions << y[0] << ' ' << y[1] << ' ' << y[2] << '\n';
    }
  }
  feasible_solutions.flush();

  vector<glm::vec2> uv_points{};
  gnuplot::temporary_file projected_solutions{};
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
    projected_solutions << y[0] << ' ' << y[1] << ' ' << y[2] << '\n';
  }
  projected_solutions.flush();

  delaunay::triangulator triangulation(uv_points);

  vector<real> distances;
  real mean_distance = 0;
  real p[3], q[3];
  for (auto &e : triangulation.quad_edges) {
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
    if (distances[i] < mean_distance)
      continue;
    // if (distances[i] / distances[i + 1] > 0.8) continue;
    if (distances[i] / distances[i + 1] > 0.985)
      continue;
    max_allowed_distance = distances[i];
    std::cout << "index = " << i << '\n'
              << "max allowed dist = " << max_allowed_distance << '\n';
    break;
  }

  gnuplot::temporary_file delaunay_triangulation{};
  for (auto &e : triangulation.quad_edges) {
    for (int i = 0; i < 4; i += 2) {
      const auto p = triangulation.vertices[e[i].data];
      y[0] = p[0] * pu[0] + p[1] * pv[0];
      y[1] = p[0] * pu[1] + p[1] * pv[1];
      y[2] = p[0] * pu[2] + p[1] * pv[2];
      y[0] = y[0] * (max_y[0] - min_y[0]) + min_y[0];
      y[1] = y[1] * (max_y[1] - min_y[1]) + min_y[1];
      y[2] = y[2] * (max_y[2] - min_y[2]) + min_y[2];
      delaunay_triangulation << y[0] << ' ' << y[1] << ' ' << y[2] << ' ';

      ranges::copy(frontier.objectives(triangulation.permutation[e[i].data]),
                   y);
      delaunay_triangulation << y[0] << ' ' << y[1] << ' ' << y[2] << '\n';
    }
    delaunay_triangulation << '\n' << '\n';
  }
  delaunay_triangulation.flush();

  gnuplot::temporary_file pareto_frontier{};
  for (auto &e : triangulation.quad_edges) {
    ranges::copy(frontier.objectives(triangulation.permutation[e[0].data]), p);
    ranges::copy(frontier.objectives(triangulation.permutation[e[2].data]), q);
    real distance = 0;
    for (size_t k = 0; k < 3; ++k) {
      const auto t = (p[k] - q[k]) / (max_y[k] - min_y[k]);
      distance += t * t;
    }
    distance = sqrt(distance);

    if (distance > max_allowed_distance)
      continue;
    pareto_frontier << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
    pareto_frontier << q[0] << ' ' << q[1] << ' ' << q[2] << '\n';
    pareto_frontier << '\n' << '\n';
  }
  pareto_frontier.flush();

  gnuplot::temporary_file pareto_solutions{};
  for (size_t i = 0; i < frontier.sample_count(); ++i) {
    for (auto x : frontier.objectives(i))
      pareto_solutions << x << ' ';
    pareto_solutions << '\n';
  }
  pareto_solutions.flush();

  constexpr char default_settings[] = //
                                      // "set size square 1,1,1\n"
                                      // "set key left bottom\n"
      "set key spacing 4\n"
      "set xr [-22:-13]\n"
      "set yr [-20:1]\n"
      "set zr [-0.5:2]\n"
      "set xl 'Objective 1'\n"
      "set yl 'Objective 2'\n"
      "set zl 'Objective 3'\n"
      "set title 'Complex Problem with Three Objectives'\n";

  gnuplot::pipe plot{};
  plot //
       // << "set terminal qt 1 size 900,900 font 'Times,14'\n"
       // << default_settings
       // << format("splot '{}' u 1:2:3 w p pt 6 ps 1 lt rgb '#888888' title "
       //           "'Feasible Solutions'\n",
       //           feasible_solutions.path().string())
       // << format("replot '{}' u 1:2:3 w p pt 13 ps 1.5 lt rgb '#2255ff'
       // title"
       //           "'Pareto Solutions'\n",
       //           pareto_solutions.path().string());

      //  << "set terminal qt 2 size 900,900 font 'Times,14'\n"
      //  << default_settings
      //  << format("splot '{}' u 1:2:3 w p pt 6 ps 1 lt rgb '#888888' title "
      //            "'Feasible Solutions'\n",
      //            feasible_solutions.path().string())
      //  << format("replot '{}' u 1:2:3 w p pt 13 ps 1.5 lt rgb '#2255ff'
      //  title"
      //            "'Pareto Solutions'\n",
      //            pareto_solutions.path().string())
      //  << format("replot '{}' u 1:2:3 w p pt 13 ps 1.5 lt rgb '#2255ff'
      //  title"
      //            "'Projected Pareto Solutions'\n",
      //            projected_solutions.path().string())
      //  << format("replot '{}' u 1:2:3 w l lt rgb '#ff5522' title "
      //            "'Projected Triangulation'\n",
      //            delaunay_triangulation.path().string())

      //  << "set terminal qt 3 size 900,900 font 'Times,14'\n"
      //  << default_settings
      //  << format("splot '{}' u 1:2:3 w p pt 6 ps 1 lt rgb '#888888' title "
      //            "'Feasible Solutions'\n",
      //            feasible_solutions.path().string())
      //  << format("replot '{}' u 1:2:3 w p pt 13 ps 1.5 lt rgb '#2255ff'
      //  title"
      //            "'Pareto Solutions'\n",
      //            pareto_solutions.path().string())
      //  << format("replot '{}' u 4:5:6 w l lt rgb '#ff5522' title "
      //            "'Projected Triangulation'\n",
      //            delaunay_triangulation.path().string())

      << "set terminal qt 1 size 900,900 enhanced font 'Times,14'\n"
      << default_settings
      << format("splot '{}' u 1:2:3 w p pt 6 ps 1 lt rgb '#888888' title "
                "'Feasible Solutions'\n",
                feasible_solutions.path().string())
      << format("replot '{}' u 1:2:3 w p pt 13 ps 1.5 lt rgb '#2255ff' title"
                "'Pareto Solutions'\n",
                pareto_solutions.path().string())
      << format("replot '{}' u 1:2:3 w l lt rgb '#ff5522' title "
                "'Pareto Frontier'\n",
                pareto_frontier.path().string());
}
