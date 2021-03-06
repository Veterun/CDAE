#ifndef _LIBCF_BPR_HPP_
#define _LIBCF_BPR_HPP_

#include <algorithm>
#include <base/heap.hpp>
#include <base/utils.hpp>
#include <model/loss.hpp>
#include <model/recsys/imf.hpp>

namespace libcf {

struct BPRConfig {
  BPRConfig() = default;
  double learn_rate = 0.1;
  double beta = 1.;
  double lambda = 0.01;  // regularization coefficient 
  LossType lt = LOG; // loss type
  PenaltyType pt = L2;  // penalty type
  size_t num_dim = 10;
  size_t num_neg = 5;
  bool using_bias_term = true;
  bool using_adagrad = true;
};

class BPR : public IMF {

 public:
  BPR(const BPRConfig& mcfg) {  
    learn_rate_ = mcfg.learn_rate;
    beta_ = mcfg.beta;
    lambda_ = mcfg.lambda;
    num_dim_ = mcfg.num_dim;
    num_neg_ = mcfg.num_neg;
    using_bias_term_ = mcfg.using_bias_term;
    using_adagrad_ = mcfg.using_adagrad;
    loss_ = Loss::create(mcfg.lt);
    penalty_ = Penalty::create(mcfg.pt);

    LOG(INFO) << "BPR Model Configure: \n" 
        << "\t{lambda: " << lambda_ << "}, "
        << "{Learn Rate: " << learn_rate_ << "}, "
        << "{Beta " << beta_ << "}, "
        << "{Loss: " << loss_->loss_type() << "}, "
        << "{Penalty: " << penalty_->penalty_type() << "}\n"
        << "\t{Dim: " << num_dim_ << "}, "
        << "{BiasTerm: " << using_bias_term_ << "}, "
        << "{Using AdaGrad: " << using_adagrad_ << "}, "
        << "{Num Negative: " << num_neg_ << "}";
  }

  //BPR() : BPR(BPRConfig()) {}

  void reset(const Data& data_set) {
    IMF::reset(data_set);
  }
 
  virtual void train_one_iteration(const Data& train_data) {
    for (size_t uid = 0; uid < num_users_; ++uid) {
      auto fit = user_rated_items_.find(uid);
      CHECK(fit != user_rated_items_.end());
      auto& item_map = fit->second;
      for (auto& p : item_map) {
        auto& iid = p.first;
        for (size_t idx = 0; idx < num_neg_; ++idx) {
          size_t jid = sample_negative_item(item_map);
          train_one_pair(uid, iid, jid, 1.);
        }
      }
    }
  }

  virtual void train_one_pair(size_t uid, size_t iid, size_t jid, double rui) {
    double pred_i = predict_user_item_rating(uid, iid);
    double pred_j = predict_user_item_rating(uid, jid);
    double pred_ij = pred_i - pred_j;
    double gradient = loss_->gradient(pred_ij, rui);

    double ib_grad = gradient + 2. * lambda_ * ib_(iid);
    double jb_grad = - gradient + 2. * lambda_ * ib_(jid);
    DVector uv_grad = gradient * (iv_.row(iid) - iv_.row(jid)) + 2. * lambda_ * uv_.row(uid);
    DVector iv_grad = gradient * uv_.row(uid) + 2. * lambda_ * iv_.row(iid);
    DVector jv_grad = - gradient * uv_.row(uid) + 2. * lambda_ * iv_.row(jid);

    if (using_adagrad_) {
      if (using_bias_term_) {
        ib_ag_(iid) += ib_grad * ib_grad;
        ib_ag_(jid) += jb_grad * jb_grad;
        ib_grad /= (beta_ + std::sqrt(ib_ag_(iid)));
        jb_grad /= (beta_ + std::sqrt(ib_ag_(jid)));
      }
      uv_ag_.row(uid) += uv_grad.cwiseProduct(uv_grad);
      iv_ag_.row(iid) += iv_grad.cwiseProduct(iv_grad);
      iv_ag_.row(jid) += jv_grad.cwiseProduct(jv_grad);
      uv_grad = uv_grad.cwiseQuotient((uv_ag_.row(uid).cwiseSqrt().transpose().array() + beta_).matrix());
      iv_grad = iv_grad.cwiseQuotient((iv_ag_.row(iid).cwiseSqrt().transpose().array() + beta_).matrix());
      jv_grad = jv_grad.cwiseQuotient((iv_ag_.row(jid).cwiseSqrt().transpose().array() + beta_).matrix());
    }

    if (using_bias_term_) {
      ib_(iid) -= learn_rate_ * ib_grad;
      ib_(jid) -= learn_rate_ * jb_grad;
    }
    uv_.row(uid) -= learn_rate_ * uv_grad;
    iv_.row(iid) -= learn_rate_ * iv_grad;
    iv_.row(jid) -= learn_rate_ * jv_grad;
  }
};

} // namespace

#endif // _LIBCF_BPR_HPP_
