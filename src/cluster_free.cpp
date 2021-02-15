#include <RcppEigen.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <mutex>
#include <functional>
#include <sccore_par.hpp>

#include <progress.hpp>
#include <unistd.h>

using namespace Rcpp;
using namespace Eigen;

/// Utils

double median(std::vector<double> &vec) {
    assert(!vec.empty());
    const auto median_it1 = vec.begin() + vec.size() / 2;
    std::nth_element(vec.begin(), median_it1 , vec.end());

    if (vec.size() % 2 != 0)
        return *median_it1;

    const auto median_it2 = vec.begin() + vec.size() / 2 - 1;
    std::nth_element(vec.begin(), median_it2 , vec.end());
    return (*median_it1 + *median_it2) / 2;
}

Eigen::MatrixXd collapseMatrixNorm(const Eigen::SparseMatrix<double> &mtx, const std::vector<int> &factor,
                                   const std::vector<int> &nn_ids, const std::vector<unsigned> &n_obs_per_samp) {
    assert(mtx.rows() == factor.size());
    MatrixXd res = MatrixXd::Zero(mtx.rows(), n_obs_per_samp.size());
    for (int id : nn_ids) {
        int fac = factor[id];
        if (fac >= n_obs_per_samp.size() || fac < 0)
            stop("Wrong factor: " + std::to_string(fac) + ", id: " + std::to_string(id));

        for (SparseMatrix<double, ColMajor>::InnerIterator gene_it(mtx, id); gene_it; ++gene_it) {
            res(gene_it.row(), fac) += gene_it.value() / n_obs_per_samp.at(fac);
        }
    }

    return res;
}

/// Z-scores

std::vector<unsigned> count_values(const std::vector<int> &values, const std::vector<int> &sub_ids) {
    int n_vals = 0;
    for (int i : sub_ids) {
        int v = values.at(i);
        if (v < 0) stop("sample_per_cell must contain only positive factors");
        n_vals = std::max(n_vals, v + 1);
    }

    std::vector<unsigned> counts(n_vals, 0);
    for (int id : sub_ids) {
        counts[values[id]]++;
    }

    return counts;
}

std::vector<double> estimateCellZScore(const SparseMatrix<double> &cm, const std::vector<int> &sample_per_cell,
                                       const std::vector<int> &nn_ids, const std::vector<bool> &is_ref,
                                       const int min_n_samp_per_cond, const int min_n_obs_per_samp, bool robust) {
    assert(cm.rows() == is_ref.size());
    auto n_ids_per_samp = count_values(sample_per_cell, nn_ids);
    auto mat_collapsed = collapseMatrixNorm(cm, sample_per_cell, nn_ids, n_ids_per_samp);

    std::vector<double> res;
    for (int gi = 0; gi < mat_collapsed.rows(); ++gi) {
        std::vector<double> ref_vals, target_vals;
        auto g_vec = mat_collapsed.row(gi);
        for (int si = 0; si < g_vec.size(); ++si) {
            if (n_ids_per_samp[si] < min_n_obs_per_samp)
                continue;

            if (is_ref.at(si)) {
                ref_vals.emplace_back(g_vec[si]);
            } else {
                target_vals.emplace_back(g_vec[si]);
            }
        }

        if (std::min(target_vals.size(), ref_vals.size()) < min_n_samp_per_cond || ref_vals.size() < 2) {
            res.emplace_back(NAN);
            continue;
        }

        double m_ref, m_targ, sd_ref = 0;
        if (robust) {
            m_ref = median(ref_vals);
            m_targ = median(target_vals);
            std::vector<double> diffs;
            for (double v : ref_vals) {
                diffs.emplace_back(std::abs(v - m_ref));
            }
            sd_ref = median(diffs) * 1.4826;
        } else {
            m_ref = std::accumulate(ref_vals.begin(), ref_vals.end(), 0.0) / ref_vals.size();
            m_targ = std::accumulate(target_vals.begin(), target_vals.end(), 0.0) / target_vals.size();
            for (double v : ref_vals) {
                sd_ref += (v - m_ref) * (v - m_ref);
            }
            sd_ref = sqrt(sd_ref / (ref_vals.size() - 1));
        }
        double z = (sd_ref < 1e-20) ? NAN : ((m_targ - m_ref) / sd_ref);
        res.emplace_back(z);
    }

    return res;
}

// [[Rcpp::export]]
SEXP clusterFreeZScoreMat(const SEXP count_mat, IntegerVector sample_per_cell, List nn_ids, const std::vector<bool> &is_ref,
                          const int min_n_samp_per_cond=2, const int min_n_obs_per_samp=1, bool robust=true,
                          const double min_z=0.01, bool verbose=true, int n_cores=1) {
    const auto samp_per_cell_c = as<std::vector<int>>(IntegerVector(sample_per_cell - 1));
    if (sample_per_cell.size() == 0 || (*std::max_element(samp_per_cell_c.begin(), samp_per_cell_c.end()) <= 0))
        stop("sample_per_cell must be a factor vector with non-empty levels");

    std::vector<std::vector<int>> nn_ids_c;
    for (auto &ids : nn_ids) {
        nn_ids_c.emplace_back(as<std::vector<int>>(ids));
    }

    std::vector<double> res_scores(nn_ids_c.size(), 0);

    auto cm = as<SparseMatrix<double>>(count_mat);
    std::vector<Triplet<double>> z_triplets;
    std::mutex mut;
    auto task = [&cm, &samp_per_cell_c, &nn_ids_c, &is_ref, &min_n_samp_per_cond, &min_n_obs_per_samp, &min_z, &robust, &z_triplets, &mut](int ci) {
        auto cell_zs = estimateCellZScore(cm, samp_per_cell_c, nn_ids_c[ci], is_ref, min_n_samp_per_cond, min_n_obs_per_samp, robust);
        VectorXd expr = cm.col(ci);
        for (int gi = 0; gi < cell_zs.size(); ++gi) { // TODO: optimize for sparse matrix operators
            if (expr(gi) < 1e-20)
                continue;

            double z_cur = cell_zs[gi];
            if (std::isnan(z_cur) || std::abs(z_cur) >= min_z) {
                std::lock_guard<std::mutex> l(mut);
                z_triplets.emplace_back(gi, ci, z_cur);
            }
        }

    };
    sccore::runTaskParallelFor(0, nn_ids_c.size(), task, n_cores, verbose);

    SparseMatrix<double> z_mat(cm.rows(), cm.cols());
    z_mat.setFromTriplets(z_triplets.begin(), z_triplets.end());

    S4 z_mat_r(wrap(z_mat));
    z_mat_r.slot("Dimnames") = S4(count_mat).slot("Dimnames");

    return z_mat_r;
}

////// Expression shifts

double estimateCorrelationDistance(const Eigen::VectorXd &v1, const Eigen::VectorXd &v2, bool centered) {
    if (v1.size() != v2.size())
        stop("Vectors must have the same length");

    double m1 = 0, m2 = 0;
    if (centered) {
        m1 = v1.mean();
        m2 = v2.mean();
    }

    double vp = 0, v1s = 0, v2s = 0;
    for (size_t i = 0; i < v1.size(); ++i) {
        if (std::isnan(v1[i]) || std::isnan(v2[i]))
            return NAN;

        double e1 = (v1[i] - m1), e2 = (v2[i] - m2);
        vp += e1 * e2;
        v1s += e1 * e1;
        v2s += e2 * e2;
    }

    return 1 - vp / std::max(std::sqrt(v1s) * std::sqrt(v2s), 1e-10);
}

inline double average(double val1, double val2) {
    return (val1 + val2) / 2;
}

double estimateKLDivergence(const Eigen::VectorXd &v1, const Eigen::VectorXd &v2) {
    double res = 0;
    if (v1.size() != v2.size())
        stop("Vectors must have the same length");

    for (size_t i = 0; i < v1.size(); ++i) {
        double d1 = v1[i], d2 = v2[i];
        if (std::isnan(d1) || std::isnan(d2))
            return NAN;

        if (d1 > 1e-10 && d2 > 1e-10) {
            res += std::log(d1 / d2) * d1;
        }
    }

    return res;
}

double estimateJSDivergence(const Eigen::VectorXd &v1, const Eigen::VectorXd &v2) {
    if (v1.size() != v2.size())
        stop("Vectors must have the same length");

    VectorXd avg = VectorXd::Zero(v1.size());
    std::transform(v1.data(), v1.data() + v1.size(), v2.data(), avg.data(), average);

    double d1 = estimateKLDivergence(v1, avg);
    double d2 = estimateKLDivergence(v2, avg);

    return std::sqrt(0.5 * (d1 + d2));
}

//' @param sample_per_cell must contains ids from 0 to n_samples-1
//' @param n_samples must be equal to maximum(sample_per_cell) + 1
double estimateCellExpressionShift(const SparseMatrix<double> &cm, const std::vector<int> &sample_per_cell,
                                   const std::vector<int> &nn_ids, const std::vector<bool> &is_ref,
                                   const int min_n_between, const int min_n_within, const int min_n_obs_per_samp, bool norm_all,
                                   const std::string &dist = "cosine", bool log_vecs=false) {
    auto n_ids_per_samp = count_values(sample_per_cell, nn_ids);
    auto mat_collapsed = collapseMatrixNorm(cm, sample_per_cell, nn_ids, n_ids_per_samp);
    if (log_vecs) {
        for (int i = 0; i < mat_collapsed.size(); ++i) {
            mat_collapsed(i) = std::log10(1e3 * mat_collapsed(i) + 1);
        }
    }

    std::vector<double> within_dists, between_dists;
    for (int s1 = 0; s1 < n_ids_per_samp.size(); ++s1) {
        if (n_ids_per_samp.at(s1) < min_n_obs_per_samp)
            continue;

        auto v1 = mat_collapsed.col(s1);
        for (int s2 = s1 + 1; s2 < n_ids_per_samp.size(); ++s2) {
            if (n_ids_per_samp.at(s2) < min_n_obs_per_samp)
                continue;

            auto v2 = mat_collapsed.col(s2);
            double d;
            if (dist == "cosine") {
                d = estimateCorrelationDistance(v1, v2, false);
            } else if (dist == "js") {
                d = estimateJSDivergence(v1, v2);
            } else if (dist == "cor") {
                d = estimateCorrelationDistance(v1, v2, true);
            } else {
                stop("Unknown dist: ", dist);
            }

            bool is_within = norm_all ? (is_ref.at(s1) == is_ref.at(s2)) : (is_ref.at(s1) && is_ref.at(s2));
            if (is_within) {
                within_dists.push_back(d);
            } else if (is_ref.at(s1) != is_ref.at(s2)) {
                between_dists.push_back(d);
            }
        }
    }

    if ((within_dists.size() < min_n_within) || (between_dists.size() < min_n_between))
        return NAN;

    return median(between_dists) / median(within_dists);
}

// [[Rcpp::export]]
NumericVector estimateClusterFreeExpressionShiftsC(const Eigen::SparseMatrix<double> &cm, IntegerVector sample_per_cell, List nn_ids, const std::vector<bool> &is_ref,
                                                   const int min_n_between=1, const int min_n_within=1, const int min_n_obs_per_samp=1, bool norm_all=false,
                                                   bool verbose=true, int n_cores=1, const std::string &dist="cosine", bool log_vecs=false) {
    const auto samp_per_cell_c = as<std::vector<int>>(IntegerVector(sample_per_cell - 1));
    if (sample_per_cell.size() == 0 || (*std::max_element(samp_per_cell_c.begin(), samp_per_cell_c.end()) <= 0))
        stop("sample_per_cell must be a factor vector with non-empty levels");

    std::vector<std::vector<int>> nn_ids_c;
    for (auto &ids : nn_ids) {
        nn_ids_c.emplace_back(as<std::vector<int>>(ids));
    }

    std::vector<double> res_scores(nn_ids_c.size(), 0);

    auto task = [&cm, &samp_per_cell_c, &nn_ids_c, &is_ref, &res_scores, &min_n_between, &min_n_within, &min_n_obs_per_samp, &norm_all,
            dist, log_vecs](int i) {
        res_scores[i] = estimateCellExpressionShift(cm, samp_per_cell_c, nn_ids_c[i], is_ref,
                                                    min_n_between, min_n_within, min_n_obs_per_samp, norm_all,
                                                    dist, log_vecs);
    };
    sccore::runTaskParallelFor(0, nn_ids_c.size(), task, n_cores, verbose);

    NumericVector res = wrap(res_scores);
    res.attr("names") = nn_ids.names();

    return res;
}
