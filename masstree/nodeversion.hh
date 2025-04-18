/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2013 President and Fellows of Harvard College
 * Copyright (c) 2012-2013 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Masstree LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Masstree LICENSE file; the license in that file
 * is legally binding.
 */
#ifndef MASSTREE_NODEVERSION_HH
#define MASSTREE_NODEVERSION_HH
#include "compiler.hh"

template <typename P>
class basic_nodeversion {
 public:
  typedef P traits_type;
  typedef typename P::value_type value_type;

  basic_nodeversion() {}
  explicit basic_nodeversion(bool isleaf) {
    v_ = isleaf ? (value_type)P::isleaf_bit : 0;
  }

  bool isleaf() const { return v_ & P::isleaf_bit; }

  basic_nodeversion<P> stable() const { return stable(relax_fence_function()); }
  template <typename SF>
  basic_nodeversion<P> stable(SF spin_function) const {
    value_type x = v_;
    while (x & P::dirty_mask) {
      spin_function();
      x = v_;
    }
    acquire_fence();
    return x;
  }
  template <typename SF>
  basic_nodeversion<P> stable_annotated(SF spin_function) const {
    value_type x = v_;
    while (x & P::dirty_mask) {
      spin_function(basic_nodeversion<P>(x));
      x = v_;
    }
    acquire_fence();
    return x;
  }

  bool locked() const { return v_ & P::lock_bit; }
  bool inserting() const { return v_ & P::inserting_bit; }
  bool splitting() const { return v_ & P::splitting_bit; }
  bool deleted() const { return v_ & P::deleted_bit; }
  bool has_changed(basic_nodeversion<P> x) const {
    fence();
    return (x.v_ ^ v_) > P::lock_bit;
  }
  bool has_split() const { return !(v_ & P::root_bit); }
  bool has_split(basic_nodeversion<P> x) const {
    fence();
    return (x.v_ ^ v_) >= P::vsplit_lowbit;
  }
  bool simple_has_split(basic_nodeversion<P> x) const {
    return (x.v_ ^ v_) >= P::vsplit_lowbit;
  }

  basic_nodeversion<P> lock() { return lock(*this); }
  basic_nodeversion<P> lock(basic_nodeversion<P> expected) {
    return lock(expected, relax_fence_function());
  }
  template <typename SF>
  basic_nodeversion<P> lock(basic_nodeversion<P> expected, SF spin_function) {
    pcontext::lock();
    while (1) {
      if (!(expected.v_ & P::lock_bit) &&
          bool_cmpxchg(&v_, expected.v_, expected.v_ | P::lock_bit))
        break;
      spin_function();
      expected.v_ = v_;
    }
    masstree_invariant(!(expected.v_ & P::dirty_mask));
    expected.v_ |= P::lock_bit;
    acquire_fence();
    masstree_invariant(expected.v_ == v_);
    pcontext::unlock();
    return expected;
  }

  void unlock() { unlock(*this); }
  void unlock(basic_nodeversion<P> x) {
    masstree_invariant((fence(), x.v_ == v_));
    masstree_invariant(x.v_ & P::lock_bit);
    if (x.v_ & P::splitting_bit)
      x.v_ = (x.v_ + P::vsplit_lowbit) & P::split_unlock_mask;
    else
      x.v_ = (x.v_ + ((x.v_ & P::inserting_bit) << 2)) & P::unlock_mask;
    release_fence();
    v_ = x.v_;
  }

  void mark_insert() {
    masstree_invariant(locked());
    v_ |= P::inserting_bit;
    acquire_fence();
  }
  basic_nodeversion<P> mark_insert(basic_nodeversion<P> current_version) {
    masstree_invariant((fence(), v_ == current_version.v_));
    masstree_invariant(current_version.v_ & P::lock_bit);
    v_ = (current_version.v_ |= P::inserting_bit);
    acquire_fence();
    return current_version;
  }
  void mark_split() {
    masstree_invariant(locked());
    v_ |= P::splitting_bit;
    acquire_fence();
  }
  void mark_change(bool is_split) {
    masstree_invariant(locked());
    v_ |= (is_split + 1) << P::inserting_shift;
    acquire_fence();
  }
  basic_nodeversion<P> mark_deleted() {
    masstree_invariant(locked());
    v_ |= P::deleted_bit | P::splitting_bit;
    acquire_fence();
    return *this;
  }
  void mark_deleted_tree() {
    masstree_invariant(locked() && !has_split());
    v_ |= P::deleted_bit;
    acquire_fence();
  }
  void mark_root() {
    v_ |= P::root_bit;
    acquire_fence();
  }
  void mark_nonroot() {
    v_ &= ~P::root_bit;
    acquire_fence();
  }

  void assign_version(basic_nodeversion<P> x) { v_ = x.v_; }

  value_type version_value() const { return v_; }
  value_type unlocked_version_value() const { return v_ & P::unlock_mask; }

 private:
  value_type v_;

  basic_nodeversion(value_type v) : v_(v) {}
};

template <typename P>
class basic_singlethreaded_nodeversion {
 public:
  typedef P traits_type;
  typedef typename P::value_type value_type;

  basic_singlethreaded_nodeversion() {}
  explicit basic_singlethreaded_nodeversion(bool isleaf) {
    v_ = isleaf ? (value_type)P::isleaf_bit : 0;
  }

  bool isleaf() const { return v_ & P::isleaf_bit; }

  basic_singlethreaded_nodeversion<P> stable() const { return *this; }
  template <typename SF>
  basic_singlethreaded_nodeversion<P> stable(SF) const {
    return *this;
  }
  template <typename SF>
  basic_singlethreaded_nodeversion<P> stable_annotated(SF) const {
    return *this;
  }

  bool locked() const { return false; }
  bool inserting() const { return false; }
  bool splitting() const { return false; }
  bool deleted() const { return false; }
  bool has_changed(basic_singlethreaded_nodeversion<P>) const { return false; }
  bool has_split() const { return !(v_ & P::root_bit); }
  bool has_split(basic_singlethreaded_nodeversion<P>) const { return false; }
  bool simple_has_split(basic_singlethreaded_nodeversion<P>) const {
    return false;
  }

  basic_singlethreaded_nodeversion<P> lock() { return *this; }
  basic_singlethreaded_nodeversion<P> lock(
      basic_singlethreaded_nodeversion<P>) {
    return *this;
  }
  template <typename SF>
  basic_singlethreaded_nodeversion<P> lock(basic_singlethreaded_nodeversion<P>,
                                           SF) {
    return *this;
  }

  void unlock() {}
  void unlock(basic_singlethreaded_nodeversion<P>) {}

  void mark_insert() {}
  basic_singlethreaded_nodeversion<P> mark_insert(
      basic_singlethreaded_nodeversion<P>) {
    return *this;
  }
  void mark_split() { v_ &= ~P::root_bit; }
  void mark_change(bool is_split) {
    if (is_split) mark_split();
  }
  basic_singlethreaded_nodeversion<P> mark_deleted() { return *this; }
  void mark_deleted_tree() { v_ |= P::deleted_bit; }
  void mark_root() { v_ |= P::root_bit; }
  void mark_nonroot() { v_ &= ~P::root_bit; }

  void assign_version(basic_singlethreaded_nodeversion<P> x) { v_ = x.v_; }

  value_type version_value() const { return v_; }
  value_type unlocked_version_value() const { return v_; }

 private:
  value_type v_;
};

struct nodeversion32_parameters {
  enum {
    lock_bit = (1U << 0),
    inserting_shift = 1,
    inserting_bit = (1U << 1),
    splitting_bit = (1U << 2),
    dirty_mask = inserting_bit | splitting_bit,
    vinsert_lowbit = (1U << 3),  // == inserting_bit << 2
    vsplit_lowbit = (1U << 9),
    unused1_bit = (1U << 28),
    deleted_bit = (1U << 29),
    root_bit = (1U << 30),
    isleaf_bit = (1U << 31),
    split_unlock_mask = ~(root_bit | unused1_bit | (vsplit_lowbit - 1)),
    unlock_mask = ~(unused1_bit | (vinsert_lowbit - 1)),
    top_stable_bits = 4
  };

  typedef uint32_t value_type;
};

struct nodeversion64_parameters {
  enum {
    lock_bit = (1ULL << 8),
    inserting_shift = 9,
    inserting_bit = (1ULL << 9),
    splitting_bit = (1ULL << 10),
    dirty_mask = inserting_bit | splitting_bit,
    vinsert_lowbit = (1ULL << 11),  // == inserting_bit << 2
    vsplit_lowbit = (1ULL << 27),
    unused1_bit = (1ULL << 60),
    deleted_bit = (1ULL << 61),
    root_bit = (1ULL << 62),
    isleaf_bit = (1ULL << 63),
    split_unlock_mask = ~(root_bit | unused1_bit | (vsplit_lowbit - 1)),
    unlock_mask = ~(unused1_bit | (vinsert_lowbit - 1)),
    top_stable_bits = 4
  };

  typedef uint64_t value_type;
};

typedef basic_nodeversion<nodeversion32_parameters> nodeversion;
typedef basic_singlethreaded_nodeversion<nodeversion32_parameters>
    singlethreaded_nodeversion;

#endif
