//! c/c++ headers
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
//! dependency headers
#include <cppad/ipopt/solve.hpp>
#include <ortools/linear_solver/linear_solver.h>  // NOLINT [build/include_order]
//! project headers
#include "correspondences/correspondences.hpp"

//! namespaces
namespace gor = operations_research;  // from ORTools
namespace cor = correspondences;

/**
 * @brief compute pairwise consistency score; see Eqn. 49 from reference
 *
 * @param [in] si ith point in source distribution
 * @param [in] tj jth point in target distribution
 * @param [in] sk kth point in source distribution
 * @param [in] tl lth point in target distribution
 * @return pairwise consistency score for (i, j, k, l)
 */
double cor::consistency(arma::vec3 const & si, arma::vec3 const & tj, arma::vec3 const & sk,
        arma::vec3 const & tl) noexcept {
    double const dist_si_to_sk = arma::norm(si - sk, 2);
    double const dist_tj_to_tl = arma::norm(tj - tl, 2);
    return std::abs(dist_si_to_sk - dist_tj_to_tl);
}

/**
 * @brief generate weight tensor for optimation objective; see Eqn. 48 from reference
 *
 * @param [in] src_pts points to transform
 * @param [in] dst_pts target points
 * @param [in] config `Config` instance with optimization parameters; see `Config` definition
 * @return weight tensor for optimization objective; `w_{ijkl}` from reference
 */
cor::WeightTensor cor::generate_weight_tensor(arma::mat const & source_pts, arma::mat const & target_pts,
    cor::Config const & config) noexcept {
    WeightTensor weight = {};
    auto const & eps = config.epsilon;
    auto const & pw_thresh = config.pairwise_dist_threshold;
    size_t const & m = source_pts.n_cols;
    size_t const & n = target_pts.n_cols;
    for (size_t i = 0; i < m; ++i) {
        for (size_t j = 0; j < n; ++j) {
            for (size_t k = 0; k < m; ++k) {
                for (size_t l = 0; l < n; ++l) {
                    if (i != k && j != l) {
                        arma::vec3 const si = arma::vec3(source_pts.col(i));
                        arma::vec3 const tj = arma::vec3(target_pts.col(j));
                        arma::vec3 const sk = arma::vec3(source_pts.col(k));
                        arma::vec3 const tl = arma::vec3(target_pts.col(l));
                        double const c = consistency(si, tj, sk, tl);
                        if (c <= eps && arma::norm(si - sk, 2) >= pw_thresh && arma::norm(tj - tl, 2) >= pw_thresh) {
                            weight[std::make_tuple(i, j, k, l)] = -std::exp(-c);
                        }
                    }
                }
            }
        }
    }
    return weight;
}

/**
 * @brief Finds x that minimizes inner product <c, x> subject to:
 * (1) bounds constraints lb <= x <= ub, and
 * (2) equality constraints A*x==b
 * using Google's ORTools
 *
 * @param [in] c vector c in <c, x> above
 * @param [in] A matrix A in Ax==b above
 * @param [in] b vector b in Ax==b above
 * @param [in] lower_bound (scalar) lower bound on (individual components of) x
 * @param [in] upper_bound (scalar) upper bound on (individual components of) x
 * @param [in][out] x_opt value of x that minimizes <c, x> subject to defined constraints
 * @return
 */
bool cor::linear_programming(arma::colvec const & c, arma::mat const & A, arma::colvec const & b,
        double const & lower_bound, double const & upper_bound, arma::colvec & x_opt) noexcept {
    //! check correct size
    if (c.n_rows != A.n_cols) {
        std::cout << static_cast<std::string>(__func__)
            << ": First and third arguments must have the same number of columns" << std::endl;
        return false;
    } else if (b.n_rows != A.n_rows) {
        std::cout << static_cast<std::string>(__func__)
            << ": Second and third arguments must have the same number of columns" << std::endl;
        return false;
    } else if (c.n_rows != x_opt.n_rows) {
        std::cout << static_cast<std::string>(__func__)
            << ": First and sixth arguments must have the same number of columns" << std::endl;
        return false;
    }

    //! overwrite x_opt with infeasible values
    auto const infeasible_val = lower_bound - 1.;
    x_opt.fill(infeasible_val);

    //! setup solver
    gor::MPSolver solver("NULL", gor::MPSolver::GLOP_LINEAR_PROGRAMMING);
    std::vector<gor::MPVariable*> opt_sol;
    gor::MPObjective* const objective = solver.MutableObjective();

    //! setup optimal solution array: lower_bound le xi le upper_bound for all 0 le i lt size
    solver.MakeNumVarArray(/* size */ c.n_rows,
            /* lb */ lower_bound,
            /* ub */ upper_bound,
            /* name prefix */ "X",
            /* opt var ref */ &opt_sol);

    //! c.t * x
    for (size_t i = 0; i < c.n_rows; ++i) {
        objective->SetCoefficient(opt_sol[i], c(i));
    }

    //! minimization is the goal : min c.t * x
    objective->SetMinimization();

    //! constraints: A*x le b
    std::vector<gor::MPConstraint*> constraints;
    for (size_t i = 0; i < A.n_rows; ++i) {
        constraints.push_back( solver.MakeRowConstraint( /* lower bound */ -solver.infinity(),
                    /* upper bound */ b(i) ) );
        for (size_t j = 0; j < A.n_cols; ++j) {
            constraints.back()->SetCoefficient(opt_sol[j], A(i, j));
        }
    }

    //! find the optimal value
    // XXX(jwd) can increase num threads to be > 1 with solver.setNumThreads to speed up computation
    gor::MPSolver::ResultStatus const result_status = solver.Solve();
    //! check solution
    if (result_status != gor::MPSolver::OPTIMAL) {
        LOG(INFO) << "The problem does not have an optimal solution!";
        if (result_status == gor::MPSolver::FEASIBLE) {
            LOG(INFO) << "A potentially suboptimal solution was found";
        } else {
            LOG(INFO) << "The solver failed.";
        }
        return false;
    }

    //! solver was successful - write output and return true
    for (size_t i = 0; i < x_opt.n_rows; ++i) {
        x_opt(i) = opt_sol[i]->solution_value();
    }
    return true;
}

/** ConstrainedObjective::~ConstrainedObjective()
 * @brief destructor for constrained objective function
 *
 * @param[in]
 * @return
 *
 * @note nothing to do; resources are automatically deallocated
 */
cor::ConstrainedObjective::~ConstrainedObjective() { }

/** ConstrainedObjective::operator()
 * @brief operator overload for IPOPT
 *
 * @param[in][out] fgrad objective function evaluation (including constraints) at point `z`
 * @param[in] z point for evaluation
 * return
 */
void cor::ConstrainedObjective::operator()(cor::ConstrainedObjective::ADvector &fgrad,
        cor::ConstrainedObjective::ADvector const & z) noexcept {
    size_t curr_idx = 0;
    //! objective value
    fgrad[curr_idx] = 0.;
    for (size_t i = 0; i < m_; ++i) {
        for (size_t j = 0; j < n_; ++j) {
            for (size_t k = 0; k < m_; ++k) {
                for (size_t l = 0; l < n_; ++l) {
                    if (i != k && j != l) {
                        WeightKey_t const key = std::make_tuple(i, j, k, l);
                        if (weights_.find(key) != weights_.end()) {
                            fgrad[curr_idx] += static_cast<CppAD::AD<double>>(weights_[key]) * z[i*(n_+1) + j] * z[k*(n_+1) + l];
                        }
                    }
                }
            }
        }
    }

    //! constraints:
    for (size_t i = 0; i < m_; ++i) {
        ++curr_idx;
        for (size_t j = 0; j <= n_; ++j) {
            fgrad[curr_idx] += z[i*(n_+1) + j];
        }
    }

    ++curr_idx;
    for (size_t j = 0; j < n_; ++j) {
        fgrad[curr_idx] += z[m_*(n_+1) + j];
    }

    for (size_t j = 0; j < n_; ++j) {
        ++curr_idx;
        for (size_t i = 0; i <= m_; ++i) {
            fgrad[curr_idx] += z[i*(n_+1) + j];
        }
    }

    ++curr_idx;
    for (size_t i = 0; i < m_; ++i) {
        fgrad[curr_idx] += z[i*(n_+1) + n_];
    }
}

/** PointRegRelaxation::~PointRegRelaxation()
 * @brief destructor for optimization wrapper class
 *
 * @param[in]
 * @return
 *
 * @note nothing to do; resources are automatically deallocated
 */
cor::PointRegRelaxation::~PointRegRelaxation() { }

/** PointRegRelaxation::linear_projection
 * @brief Solve linear assignment problem:
 *  max c.t()*flatten(X) subject to linear constraints
 *
 * @note linear constraints are constructed within the function
 *
 * @param [in][out] opt_lp projection of optimal solution onto permutation matrices
 * @return true if converged, false otherwise
 */
bool cor::PointRegRelaxation::linear_projection(arma::colvec & opt_lp) const noexcept {
    auto const & m = ptr_obj_->num_source_pts();
    auto const & n = ptr_obj_->num_target_pts();
    auto const & n_con = ptr_obj_->num_constraints();
    auto const & state_len = ptr_obj_->state_length();

    //! make copy of optimum_ (with slack variables)
    arma::colvec c(optimum_);

    //! negate positive values
    c.transform( [&](double & val) { return (val < 0) ? static_cast<double>(0) : -val; } );

    arma::mat A(n_con, state_len, arma::fill::zeros);
    arma::colvec b(n_con, arma::fill::ones);

    //! \sum_i Xi,j = 1
    for (size_t i = 0; i <= m; ++i) {
        A(i, arma::span(i*(n+1), (i+1)*(n+1)-1)).fill(1);
    }

    //! \sum_j Xi,j = 1
    for (size_t i = m+1; i < n_con; ++i) {
        for (size_t j = 0; j < state_len; j+=n+1) {
            A(i, j) = 1;
        }
    }

    //! find projection
    return linear_programming(c, A, b, static_cast<double>(0), static_cast<double>(1), opt_lp);
}

/** PointRegRelaxation::find_optimum()
 * @brief run IPOPT to find optimum for optimization objective:
 * argmin f(z) subject to constraints g_i(z) == 0, 0 <= i < num_constraints
 *
 * @param[in]
 * @return
 *
 * @note see Eqn. 48 in paper and subsequent section for details
 */
cor::PointRegRelaxation::status_t cor::PointRegRelaxation::find_optimum() noexcept {
    auto const & m = ptr_obj_->num_source_pts();
    auto const & n = ptr_obj_->num_target_pts();
    auto const & k = ptr_obj_->num_min_corr();
    auto const & n_vars = ptr_obj_->state_length();
    auto const & n_constraints = ptr_obj_->num_constraints();

    Dvec z(n_vars);

    //! setup inequality constraints on variables: 0 <= z_{ij} <= 1 for all i,j
    Dvec z_lb(n_vars);
    Dvec z_ub(n_vars);
    for (size_t i = 0; i < n_vars; ++i) {
        z_lb[i] = 0.;
        z_ub[i] = 1.;
    }
    //! overwrite last value to avoid checking conditional for all iterations
    z_ub[n_vars-1] = 0.;

    //! setup constraints l_i <= g_i(z) <= u_i
    Dvec constraints_lb(n_constraints);
    Dvec constraints_ub(n_constraints);
    size_t ctr = 0;
    for (size_t i = 0; i < m; ++i) {
        constraints_lb[ctr] = 1.;
        constraints_ub[ctr++] = 1.;
    }

    //constraints_lb[ctr] = n - m;
    constraints_lb[ctr] = n - k;
    constraints_ub[ctr++] = n - k;
    for (size_t j = 0; j < n; ++j) {
        constraints_lb[ctr] = 1.;
        constraints_ub[ctr++] = 1.;
    }
    constraints_lb[ctr] = m - k;
    constraints_ub[ctr] = m - k;

    //! options for IPOPT solver
    std::string options;
    options += "Integer print_level  0\n";
    /**
     * NOTE: Setting sparse to true allows the solver to take advantage
     * of sparse routines, this makes the computation MUCH FASTER. If you
     * can uncomment 1 of these and see if it makes a difference or not but
     * if you uncomment both the computation time should go up in orders of
     * magnitude.
     */
    options += "Sparse  true        forward\n";
    options += "Sparse  true        reverse\n";
    options += "Numeric tol         0.1\n";
    options += "Numeric acceptable_tol 0.1\n";

    //! timeout period (sec).
    options += "Numeric max_cpu_time          1000.0\n";

    //! solve the problem
    CppAD::ipopt::solve_result<Dvec> solution;
    CppAD::ipopt::solve<Dvec, ConstrainedObjective>(
            options, z, z_lb, z_ub, constraints_lb,
            constraints_ub, *ptr_obj_, solution);
    //! if solver was not successful, return early
    if (solution.status != status_t::success) {
        return solution.status;
    }
    //! overwrite private member optimum_ with result and
    //! return success
    for (size_t i = 0; i < n_vars; ++i) {
        optimum_(i) = solution.x[i];
    }

    //! project onto permutation matrices; see Section 3.3 of the SDRSAC paper
    arma::colvec proj_opt(n_vars);
    if (!linear_projection(proj_opt)) {
        std::cout << "Linear projection failed; don't update optimum_" << std::endl;
    } else {
        //! overwrite optimum_
        optimum_ = proj_opt;
    }
    return status_t::success;
};

/** PointRegRelaxation::find_correspondences()
 * @brief identify pairwise correspondences between source and target set given optimum found
 * during optimization
 *
 * @param[in]
 * @return
 */
void cor::PointRegRelaxation::find_correspondences() noexcept {
    auto const & m = ptr_obj_->num_source_pts();
    auto const & n = ptr_obj_->num_target_pts();

    //! find best correspondences i->j (includes slack variables)
    arma::uvec const ids = arma::find(optimum_ >= config_.corr_threshold);
    std::list<size_t> corr_ids;
    for (size_t i = 0; i < ids.n_elem; ++i) {
        corr_ids.emplace_back(static_cast<size_t>(ids(i)));
    }

    //! i = divide(id, n+1), j = remainder(id, n+1) for correspondence i->j
    //! value is the strength of the correspondence (0 <= z_{ij} <= 1, closer to 1
    //! is better)
    for (auto const & c : corr_ids) {
        auto const key = std::pair<size_t, size_t>(c / (n+1), c % (n+1));
        double const value = optimum_(c);
        //! discount slack variables
        if (key.first != m && key.second != n) {
            correspondences_[key] = value;
        }
    }
    return;
}
