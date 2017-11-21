/*
 * Copyright (C) 1998-2017 ALPS Collaboration. See COPYRIGHT.TXT
 * All rights reserved. Use is subject to license terms. See LICENSE.TXT
 * For use in publications, see ACKNOWLEDGE.TXT
 */
#pragma once

#include <alps/alea/core.hpp>
#include <alps/alea/util.hpp>
#include <alps/alea/bundle.hpp>
#include <alps/alea/complex_op.hpp>
#include <alps/alea/computed.hpp>
#include <alps/alea/var_strategy.hpp>

#include <memory>

// Forward declarations

namespace alps { namespace alea {
    template <typename T, typename Str> class var_data;
    template <typename T, typename Str> class var_acc;
    template <typename T, typename Str> class var_result;

    template <typename T> class autocorr_acc;
    template <typename T> class autocorr_result;
}}

// Actual declarations

namespace alps { namespace alea {

/**
 * Data for variance accumulation.
 *
 * As with `mean_acc`, this class is basically a "union"-like structure,
 * which for a data series `(X[0], ... X[count_-1])` either represents the sum
 * of X[i] and the sum of X[i]*X[i] (sum state) or the sample mean and sample
 * variance of X (mean state).
 */
template <typename T, typename Strategy=circular_var>
class var_data
{
public:
    typedef typename bind<Strategy, T>::value_type value_type;
    typedef typename bind<Strategy, T>::var_type var_type;

public:
    var_data(size_t size);

    void reset();

    size_t size() const { return data_.rows(); }

    const size_t &count() const { return count_; }

    size_t &count() { return count_; }

    const column<value_type> &data() const { return data_; }

    column<value_type> &data() { return data_; }

    const column<var_type> &data2() const { return data2_; }

    column<var_type> &data2() { return data2_; }

    void convert_to_mean();

    void convert_to_sum();

private:
    column<T> data_;
    column<var_type> data2_;
    size_t count_;
};

template <typename T, typename Strategy>
struct traits< var_data<T,Strategy> >
{
    typedef typename bind<Strategy, T>::value_type value_type;
    typedef typename bind<Strategy, T>::var_type var_type;
};

extern template class var_data<double>;
extern template class var_data<std::complex<double>, circular_var>;
extern template class var_data<std::complex<double>, elliptic_var>;

/**
 * Accumulator which tracks the mean and a naive variance estimate.
 */
template <typename T, typename Strategy=circular_var>
class var_acc
{
public:
    typedef typename bind<Strategy, T>::value_type value_type;
    typedef typename bind<Strategy, T>::var_type var_type;

public:
    var_acc();

    var_acc(size_t size, size_t bundle_size=1);

    var_acc(const var_acc &other);

    var_acc &operator=(const var_acc &other);

    void reset();

    bool initialized() const { return initialized_; }

    bool valid() const { return (bool)store_; }

    size_t size() const { return current_.size(); }

    template <typename S>
    var_acc &operator<<(const S &obj)
    {
        computed_adapter<value_type, S> source(obj);
        return *this << (const computed<value_type> &) source;
    }

    var_acc &operator<<(const computed<value_type> &source);

    size_t count() const { return store_->count(); }

    var_result<T,Strategy> result() const;

    var_result<T,Strategy> finalize();

    const bundle<value_type> &current() const { return current_; }

    const var_data<T,Strategy> &store() const { return *store_; }

protected:
    void add_bundle();

    void uplevel(var_acc &new_uplevel) { uplevel_ = &new_uplevel; }

    void finalize_to(var_result<T,Strategy> &result);

private:
    std::unique_ptr< var_data<value_type, Strategy> > store_;
    bundle<value_type> current_;
    var_acc *uplevel_;
    bool initialized_;

    friend class autocorr_acc<T>;
};

template <typename T, typename Strategy>
struct traits< var_acc<T,Strategy> >
{
    typedef typename bind<Strategy, T>::value_type value_type;
    typedef typename bind<Strategy, T>::var_type var_type;
    typedef var_result<T,Strategy> result_type;
};

extern template class var_acc<double>;
extern template class var_acc<std::complex<double>, circular_var>;
extern template class var_acc<std::complex<double>, elliptic_var>;

/**
 * Result which contains mean and a naive variance estimate.
 */
template <typename T, typename Strategy=circular_var>
class var_result
{
public:
    typedef typename bind<Strategy, T>::value_type value_type;
    typedef typename bind<Strategy, T>::var_type var_type;

public:
    var_result() { }

    var_result(const var_data<T,Strategy> &acc_data)
        : store_(new var_data<T,Strategy>(acc_data))
    { }

    bool initialized() const { return true; }

    bool valid() const { return (bool)store_; }

    size_t size() const { return store_->size(); }

    size_t count() const { return store_->count(); }

    const column<T> &mean() const { return store_->data(); }

    const column<var_type> &var() const { return store_->data2(); }

    column<var_type> stderror() const;

    const var_data<T,Strategy> &store() const { return *store_; }

    var_data<T,Strategy> &store() { return *store_; }

    void reduce(reducer &);

    void serialize(serializer &);

private:
    std::unique_ptr< var_data<T,Strategy> > store_;

    friend class var_acc<T,Strategy>;
    friend class autocorr_result<T>;
};

template <typename T, typename Strategy>
struct traits< var_result<T,Strategy> >
{
    typedef typename bind<Strategy, T>::value_type value_type;
    typedef typename bind<Strategy, T>::var_type var_type;
};

extern template class var_result<double>;
extern template class var_result<std::complex<double>, circular_var>;
extern template class var_result<std::complex<double>, elliptic_var>;

}} /* namespace alps::alea */
