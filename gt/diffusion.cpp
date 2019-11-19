#include "diffusion.hpp"

#include "tridiagonal.hpp"

namespace diffusion {

namespace {
using gt::extent;
using gt::in_accessor;
using gt::inout_accessor;
using gt::make_param_list;
using namespace gt::expressions;

struct stage_horizontal {
  using out = inout_accessor<0>;
  using in = in_accessor<1, extent<-1, 1, -1, 1>>;

  using dx = in_accessor<2>;
  using dy = in_accessor<3>;
  using dt = in_accessor<4>;
  using coeff = in_accessor<5>;

  using param_list = make_param_list<out, in, dx, dy, dt, coeff>;

  template <typename Evaluation>
  GT_FUNCTION static void apply(Evaluation eval, full_t) {
    auto flx_x1 = eval((in(1, 0, 0) - in()) / dx());
    auto flx_x0 = eval((in() - in(-1, 0, 0)) / dx());
    auto flx_y1 = eval((in(0, 1, 0) - in()) / dy());
    auto flx_y0 = eval((in() - in(0, -1, 0)) / dy());

    eval(out()) =
        eval(in() + coeff() * dt() *
                        ((flx_x1 - flx_x0) / dx() + (flx_y1 - flx_y0) / dy()));
  }
};

struct stage_diffusion_w0 {
  using data_top = inout_accessor<0>;
  using data = in_accessor<1>;

  using param_list = make_param_list<data_top, data>;

  template <typename Evaluation>
  GT_FUNCTION static void apply(Evaluation eval, full_t::last_level) {
    eval(data_top()) = eval(data());
  }
};

struct stage_diffusion_w_forward1 {
  using data_bottom = inout_accessor<0>;
  using alpha = inout_accessor<1>;
  using beta = inout_accessor<2>;
  using gamma = inout_accessor<3>;
  using a = inout_accessor<4>;
  using b = inout_accessor<5>;
  using c = inout_accessor<6, extent<0, 0, 0, 0, -1, 0>>;
  using d = inout_accessor<7, extent<0, 0, 0, 0, -1, 0>>;

  using data = in_accessor<8, extent<0, 0, 0, 0, -1, 1>>;
  using data_top = in_accessor<9>;

  using dz = in_accessor<10>;
  using dt = in_accessor<11>;
  using coeff = in_accessor<12>;

  using param_list = make_param_list<data_bottom, alpha, beta, gamma, a, b, c,
                                     d, data, data_top, dz, dt, coeff>;

  template <typename Evaluation>
  GT_FUNCTION static void apply(Evaluation eval, full_t::first_level) {
    eval(data_bottom()) = eval(data());

    eval(a()) = real_t(0);
    eval(c()) = eval(-coeff() / (real_t(2) * dz() * dz()));
    eval(b()) = eval(real_t(1) / dt() - a() - c());
    eval(d()) = eval(real_t(1) / dt() * data() +
                     real_t(0.5) * coeff() *
                         (data_top() - real_t(2) * data() + data(0, 0, 1)) /
                         (dz() * dz()));

    eval(alpha()) = eval(beta()) = eval(-coeff() / (real_t(2) * dz() * dz()));
    eval(gamma()) = eval(-b());

    gridtools::call_proc<tridiagonal::periodic_forward1,
                         full_t::first_level>::with(eval, a(), b(), c(), d(),
                                                    alpha(), beta(), gamma());
  }

  template <typename Evaluation>
  GT_FUNCTION static void apply(Evaluation eval, full_t::modify<1, -1>) {
    eval(a()) = eval(c()) = eval(-coeff() / (real_t(2) * dz() * dz()));
    eval(b()) = eval(real_t(1) / dt() - a() - c());
    eval(d()) = eval(real_t(1) / dt() * data() +
                     real_t(0.5) * coeff() *
                         (data(0, 0, -1) - real_t(2) * data() + data(0, 0, 1)) /
                         (dz() * dz()));

    gridtools::call_proc<tridiagonal::periodic_forward1,
                         full_t::modify<1, -1>>::with(eval, a(), b(), c(), d(),
                                                      alpha(), beta(), gamma());
  }
  template <typename Evaluation>
  GT_FUNCTION static void apply(Evaluation eval, full_t::last_level) {
    eval(a()) = eval(-coeff() / (real_t(2) * dz() * dz()));
    eval(c()) = real_t(0);
    eval(b()) = eval(real_t(1) / dt() - a() - c());
    eval(d()) = eval(real_t(1) / dt() * data() +
                     real_t(0.5) * coeff() *
                         (data(0, 0, -1) - real_t(2) * data() + data_bottom()) /
                         (dz() * dz()));
    gridtools::call_proc<tridiagonal::periodic_forward1,
                         full_t::last_level>::with(eval, a(), b(), c(), d(),
                                                   alpha(), beta(), gamma());
  }
};

using stage_diffusion_w_backward1 = tridiagonal::periodic_backward1;
using stage_diffusion_w_forward2 = tridiagonal::periodic_forward2;
using stage_diffusion_w_backward2 = tridiagonal::periodic_backward2;

struct stage_diffusion_w3 {
  using out = inout_accessor<0>;
  using x = in_accessor<1>;
  using z = in_accessor<2>;
  using fact = in_accessor<3>;
  using in = in_accessor<4>;

  using dt = in_accessor<5>;

  using param_list = make_param_list<out, x, z, fact, in, dt>;

  template <typename Evaluation>
  GT_FUNCTION static void apply(Evaluation eval, full_t) {
    gridtools::call_proc<tridiagonal::periodic3, full_t>::with(eval, out(), x(),
                                                               z(), fact());
    // eval(out()) = eval((out() - in()) / dt());
  }
};

} // namespace

horizontal::horizontal(gt::grid<typename axis_t::axis_interval_t> const &grid,
                       real_t dx, real_t dy, real_t dt, real_t coeff)
    : comp_(gt::make_computation<backend_t>(
          grid, p_dx() = gt::make_global_parameter(dx),
          p_dy() = gt::make_global_parameter(dy),
          p_dt() = gt::make_global_parameter(dt),
          p_coeff() = gt::make_global_parameter(coeff),
          gt::make_multistage(
              gt::execute::parallel(),
              gt::make_stage<stage_horizontal>(p_out(), p_in(), p_dx(), p_dy(),
                                               p_dt(), p_coeff())))) {}

void horizontal::operator()(solver_state &state) {
  comp_.run(p_out() = state.tmp, p_in() = state.data);
  std::swap(state.tmp, state.data);
}

vertical::vertical(grid_t const &grid, real_t dz, real_t dt, real_t coeff)
    : sinfo_ij_(grid.i_size(), grid.j_size(), 1),
      data_top_(sinfo_ij_, "data_top"), data_bottom_(sinfo_ij_, "data_bottom"),
      alpha_(sinfo_ij_, "alpha"), beta_(sinfo_ij_, "beta"),
      gamma_(sinfo_ij_, "gamma"), fact_(sinfo_ij_, "fact"),
      z_top_(sinfo_ij_, "z_top"),
      comp_(gt::make_computation<backend_t>(
          grid, p_dz() = gt::make_global_parameter(dz),
          p_dt() = gt::make_global_parameter(dt),
          p_coeff() = gt::make_global_parameter(coeff),
          p_data_top() = data_top_, p_data_bottom() = data_bottom_,
          p_alpha() = alpha_, p_beta() = beta_, p_gamma() = gamma_,
          p_fact() = fact_, p_z_top() = z_top_,
          gt::make_multistage(
              gt::execute::forward(),
              gt::make_stage<stage_diffusion_w0>(p_data_top(), p_data_in())),
          gt::make_multistage(gt::execute::forward(),
                              gt::make_stage<stage_diffusion_w_forward1>(
                                  p_data_bottom(), p_alpha(), p_beta(),
                                  p_gamma(), p_a(), p_b(), p_c(), p_d(),
                                  p_data_in(), p_data_top(), p_dz(), p_dt(),
                                  p_coeff())),
          gt::make_multistage(
              gt::execute::backward(),
              gt::make_stage<stage_diffusion_w_backward1>(p_x(), p_c(), p_d())),
          gt::make_multistage(
              gt::execute::forward(),
              gt::make_stage<stage_diffusion_w_forward2>(
                  p_a(), p_b(), p_c(), p_d(), p_alpha(), p_gamma())),
          gt::make_multistage(gt::execute::backward(),
                              gt::make_stage<stage_diffusion_w_backward2>(
                                  p_z(), p_c(), p_d(), p_x(), p_alpha(),
                                  p_beta(), p_gamma(), p_data_top(), p_z_top(),
                                  p_fact())),
          gt::make_multistage(gt::execute::parallel(),
                              gt::make_stage<stage_diffusion_w3>(
                                  p_data_out(), p_x(), p_z(), p_fact(),
                                  p_data_in(), p_dt())))) {}

void vertical::operator()(solver_state &state) {
  comp_.run(p_data_out() = state.tmp, p_data_in() = state.data);
  std::swap(state.tmp, state.data);
}

} // namespace diffusion