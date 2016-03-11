/**
 * Copyright 2015 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#ifndef THPP_TENSORBASE_H_
#define THPP_TENSORBASE_H_

#include <iostream>
#include <string>
#include <vector>
#ifndef NO_FOLLY
#include <folly/Range.h>
#endif
#include <thpp/Storage.h>
#include <thpp/TensorPtr.h>

namespace thpp {

template <class T> class Tensor;

namespace detail {
template <class T> struct TensorOps;
}  // namespace detail

template <class T, class StorageT, class Derived>
class TensorBase {
  friend class TensorPtr<Derived>;
 protected:
  typedef detail::TensorOps<Derived> Ops;

 public:
  typedef TensorPtr<Derived> Ptr;
  typedef typename Ops::type THType;
  typedef StorageT StorageType;
  typedef T value_type;
  typedef typename Ops::accurate_type accurate_type;
  typedef long size_type;
  typedef long offset_type;
  typedef std::true_type IsRelocatable;

  template <class... Args>
  static Ptr makePtr(Args&&... args) {
    return makeTensorPtr<Derived>(std::forward<Args>(args)...);
  }

  // Ayiee. Uniform initialization and perfect forwarding don't play well
  // togethr. Explicit specialization.
  static Ptr makePtr(std::initializer_list<size_type> sizes,
                     std::initializer_list<size_type> strides =
                       std::initializer_list<size_type>()) {
    return makeTensorPtr<Derived>(std::move(sizes), std::move(strides));
  }

  Ptr copyPtr() const {
    return makePtr(*D());
  }

  THType* asTH() { return t_; }
  const THType* asTH() const { return t_; }

  // Tensor mode. Bitwise OR of:
  // UNIQUE: this tensor is unique and does not share storage with any
  //         other tensor.
  // CONTIGUOUS:  this tensor is contiguous in row-major (that is, C) order
  enum Mode : unsigned {
    UNIQUE = 1U << 0,
    CONTIGUOUS = 1U << 1,
  };

  static constexpr const char* kLuaTypeName = Ops::kLuaTypeName;

  // Force the tensor to have a certain mode. May copy data.
  void force(unsigned mode);

  // Return current mode.
  unsigned mode() const {
    return mode(t_);
  }
  static unsigned mode(const THType* th) {
    return (isUnique(th) ? UNIQUE : 0) | (isContiguous(th) ? CONTIGUOUS : 0);
  }

  // Is this tensor unique?
  bool isUnique() const {
    return isUnique(t_);
  }
  static bool isUnique(const THType* th);

  // Is this tensor contiguous?
  bool isContiguous() const {
    return isContiguous(t_);
  }
  static bool isContiguous(const THType* th);

  /// Compares two tensors for exact equality
  bool isExactlyEqual(const TensorBase& other) const;

  /// Compares two tensors for approximate equality. For integral
  /// types, forwards to isExactlyEqual; for floating point types,
  /// uses the given relativeError to determine equality.
  bool isApproximatelyEqual(const TensorBase& other,
                            float relativeError = 0.0001f) const;

  // Return number of elements.
  size_type size() const;

  // Return number of dimensions.
  int ndims() const { return t_->nDimension; }

#ifndef NO_FOLLY
  // Return list of sizes.
  LongRange sizes() const;

  // Return list of strides.
  LongRange strides() const;
#endif

  // Return a storage of sizes.
  LongStorage sizesTH() const;

  // Return a storage of strides.
  LongStorage stridesTH() const;

#ifndef NO_FOLLY
  // Return size along dimension dim.
  size_type size(int dim) const { return sizes().at(dim); }

  // Return stride along dimension dim.
  size_type stride(int dim) const { return strides().at(dim); }
#else
  size_type size(int dim) const { return sizesTH()[dim]; }
  size_type stride(int dim) const { return stridesTH()[dim]; }
#endif

  // Narrow the tensor along a given dimension; the dimension dim is narrowed
  // to [firstIndex, firstIndex + size)
  void narrow(const TensorBase& src, int dim, offset_type firstIndex,
              size_type size);
  void narrow(int dim, offset_type firstIndex, size_type size) {
    narrow(*this, dim, firstIndex, size);
  }

  // Select one slice of the tensor along a given dimension. The tensor's
  // dimensionality is reduced by 1.
  void select(const TensorBase& src, int dim, offset_type index);
  void select(int dim, offset_type index) { select(*this, dim, index); }

  // Transpose two dimensions.
  void transpose(const TensorBase& src, int dim1, int dim2);
  void transpose(int dim1, int dim2) { transpose(*this, dim1, dim2); }

  // Full transpose (reverse the order of axes)
  void transpose(const TensorBase& src) { *this = src; transpose(); }
  void transpose();

  // Unfold dimension dim along two dimensions: slices of size 'size' (with
  // given step between slices) are unfolded among a new dimension that is
  // added.
  // See http://torch5.sourceforge.net/manual/torch/index-6-8-3.html
  void unfold(const TensorBase& src, int dim, size_type size, size_type step);
  void unfold(int dim, size_type size, size_type step) {
    unfold(*this, dim, size, step);
  }

  // Squeeze: remove all 1-sized dimensions.
  void squeeze(const TensorBase& src);
  void squeeze() { squeeze(*this); }

  // Squeeze: remove one dimension if it is 1-sized.
  void squeeze(const TensorBase& src, int dim);
  void squeeze(int dim) { squeeze(*this, dim); }

  void resize(
      std::initializer_list<size_type> newSizes,
      std::initializer_list<size_type> newStrides =
        std::initializer_list<size_type>());
  void resize(LongStorage newSizes, LongStorage newStrides = LongStorage());
#ifndef NO_FOLLY
  void resize(LongRange newSizes, LongRange newStrides = LongRange());
#endif
  void resizeAs(const TensorBase& src);

  StorageType storage();
  const StorageType storage() const {
    return const_cast<TensorBase*>(this)->storage();
  }

  typedef typename std::aligned_storage<
    sizeof(StorageT), alignof(StorageT)>::type StorageBuffer;
  // Hack. You must provide an appropriately-sized buffer. Return a reference
  // to a storage object *that does not increment the reference count*,
  // so may point into nothingness if this tensor is resized or destroyed.
  // You have been warned.
  StorageType& storageRef(StorageBuffer* buf);
  const StorageType& storageRef(StorageBuffer* buf) const {
    return const_cast<TensorBase*>(this)->storageRef(buf);
  }

  offset_type storageOffset() const;

  // Fill with one value.
  void fill(T value);

  // Fill with zeros.
  void zero();

  // Given a ByteTensor of the exact same dimensionality as *this, whose
  // values are 0 or 1, set elements of *this to value iff the corresponding
  // elements in mask are 1.
  void maskedFill(const Tensor<unsigned char>& mask, T value);

  // Copy corresponding elements of src to *this iff the corresponding elements
  // in mask are 1
  void maskedCopy(const Tensor<unsigned char>& mask, const TensorBase& src);

  // Select elements from *this iff the corresponding elements in mask
  // are 1. Returns a 1d tensor with one entry for each selected element.
  Derived maskedSelect(const Tensor<unsigned char>& mask) const;

  // Select along dimension dim, copying only indices from index.
  // Returns a tensor with matching dimensionality, but only index.size()
  // elements along dimension dim.
  Derived indexSelect(int dim, const Tensor<long>& index) const;

  // Fill along dimension dim, setting entries corresponding to indices
  // from index to val.
  void indexFill(int dim, const Tensor<long>& index, T val);

  // Dot product
  accurate_type dot(const TensorBase& other) const;

  // Minimum value among all elements
  T minall() const;

  // Maximum value among all elements
  T maxall() const;

  // Sum of all elements
  accurate_type sumall() const;

  // Product of all elements
  accurate_type prodall() const;

  // Add a value to each element in the tensor
  void add(const TensorBase& src, T value);
  void add(T value) { add(*this, value); }

  // Multiply each element in the tensor by a value
  void mul(const TensorBase& src, T value);
  void mul(T value) { mul(*this, value); }

  // Divide each element in the tensor by a value
  void div(const TensorBase& src, T value);
  void div(T value) { div(*this, value); }

  // *this = a + value * b
  void cadd(const TensorBase& a, T value, const TensorBase& b);
  void cadd(T value, const TensorBase& b) { cadd(*this, value, b); }

  // *this = a .* b
  void cmul(const TensorBase& a, const TensorBase& b);
  void cmul(const TensorBase& b) { cmul(*this, b); }

  // *this = a ./ b
  void cdiv(const TensorBase& a, const TensorBase& b);
  void cdiv(const TensorBase& b) { cdiv(*this, b); }

  // *this = a + value * (b .* c)
  void addcmul(const TensorBase& a, T value, const TensorBase& b,
               const TensorBase& c);
  void addcmul(T value, const TensorBase& b, const TensorBase& c) {
    addcmul(*this, value, b, c);
  }

  // *this = a + value * (b ./ c)
  void addcdiv(const TensorBase& a, T value, const TensorBase& b,
               const TensorBase& c);
  void addcdiv(T value, const TensorBase& b, const TensorBase& c) {
    addcdiv(*this, value, b, c);
  }

  // *this = beta * t + alpha * mat * vec
  void addmv(T beta, const TensorBase& t, T alpha, const TensorBase& mat,
             const TensorBase& vec);
  void addmv(T beta, T alpha, const TensorBase& mat, const TensorBase& vec) {
    addmv(beta, *this, alpha, mat, vec);
  }

  // *this = beta * t + alpha * (m1 X m2)
  void addmm(T beta, const TensorBase& t, T alpha, const TensorBase& m1,
             const TensorBase& m2);
  void addmm(T beta, T alpha, const TensorBase& m1, const TensorBase& m2) {
    addmm(beta, *this, alpha, m1, m2);
  }

  // outer product
  // *this = beta * m + alpha * (v1 (X) v2)
  void addr(T beta, const TensorBase& m, T alpha, const TensorBase& v1,
            const TensorBase& v2);
  void addr(T beta, T alpha, const TensorBase& v1, const TensorBase& v2) {
    addr(beta, *this, alpha, v1, v2);
  }

  // number of elements, same as size()
  size_type numel() const { return size(); }

  // The following functions perform operations along one dimension.
  // The returned tensors will have the same shape as *this except that they
  // have a size of 1 along dimension dim. (That is, they're not squeezed)

  // sum
  Derived sum(int dim) const;

  // product
  Derived prod(int dim) const;

  // cumulative sum
  Derived cumsum(int dim) const;

  // cumulative product
  Derived cumprod(int dim) const;

  // Element-wise sign
  Derived sign() const;

  // TODO(tudorb): TH doesn't distinguish between a 1-element 1-dimensional
  // array (aka 1-element vector) and a scalar.
  bool isScalar() const;

  // Access the underlying data
  T* data();
  const T* data() const;

  // First element
  const T& front() const { return *data(); }
  T& front() { return *data(); }

  // Index along the first dimension
  Derived operator[](offset_type index) const;

  // Index along dimensions 0, 1, ..., indices.size() - 1.
  // Pass -1 as an index to keep that dimension unchanged.
  //
  // Example: given a 5-dimensional tensor foo,
  // foo[-1,2,-1,2,1] returns a 2-dimensional tensor corresponding
  // to the hyperplane that has d1=2, d3=2, d4=1 in foo.
  Derived operator[](std::initializer_list<offset_type> indices) const;

  // Clear the tensor.
  void clear();

  std::string str() const;

#if !defined(NO_THRIFT) && !defined(NO_FOLLY)
  // const version of serialize() that won't share, but will always copy
  void serializeUnshared(ThriftTensor& out,
                         ThriftTensorEndianness endianness =
                            ThriftTensorEndianness::NATIVE) const {
    const_cast<TensorBase*>(this)->D()->serialize(out, endianness, false);
  }
#endif

 protected:
  size_t offsetOf(std::initializer_list<offset_type> indices) const;

  static THType* cloneTH(const THType* other, unsigned cloneMode);

  explicit TensorBase(THType* t);
  ~TensorBase();
  THType* mut() const { return mut(t_); }
  static THType* mut(const THType* th) { return const_cast<THType*>(th); }

  THType* t_;

 private:
  inline Derived* D() { return static_cast<Derived*>(this); }
  inline const Derived* D() const { return static_cast<const Derived*>(this); }
};

template <class T, class StorageT, class Derived>
constexpr const char* TensorBase<T, StorageT, Derived>::kLuaTypeName;

// Unary -
template <class T, class StorageT, class Derived>
Derived operator-(const TensorBase<T, StorageT, Derived>& a);

// Binary operators. We don't define multiplication and division as they're
// ambiguous: do you mean pointwise? matrix multiplication? inner product?
// outer product?
template <class T, class StorageT, class Derived>
Derived operator+(const TensorBase<T, StorageT, Derived>& a,
                  const TensorBase<T, StorageT, Derived>& b);
template <class T, class StorageT, class Derived>
Derived& operator+=(TensorBase<T, StorageT, Derived>& a,
                    const TensorBase<T, StorageT, Derived>& b);

template <class T, class StorageT, class Derived>
Derived operator-(const TensorBase<T, StorageT, Derived>& a,
                  const TensorBase<T, StorageT, Derived>& b);
template <class T, class StorageT, class Derived>
Derived& operator-=(TensorBase<T, StorageT, Derived>& a,
                    const TensorBase<T, StorageT, Derived>& b);

// Multiplication / division by scalar
template <class T, class StorageT, class Derived>
Derived operator*(const TensorBase<T, StorageT, Derived>& a, T b);
template <class T, class StorageT, class Derived>
Derived operator*(T a, const TensorBase<T, StorageT, Derived>& b) {
  return b * a;
}
template <class T, class StorageT, class Derived>
Derived& operator*=(TensorBase<T, StorageT, Derived>& a, T b);

template <class T, class StorageT, class Derived>
Derived operator/(const TensorBase<T, StorageT, Derived>& a, T b);
template <class T, class StorageT, class Derived>
Derived& operator/=(TensorBase<T, StorageT, Derived>& a, T b);

template <class T, class StorageT, class Derived>
std::ostream& operator<<(std::ostream& s,
                         const TensorBase<T, StorageT, Derived>& t) {
  return s << t.str();
}

#ifndef NO_FOLLY
namespace detail {
template <class T>
Range<T*> makeMutable(Range<const T*> r) {
  return Range<T*>(const_cast<T*>(r.begin()), const_cast<T*>(r.end()));
}
}  // namespace detail
#endif

// Define IsTensor<T> to be used in template specializations

template <class T, class Enable=void>
struct IsTensor : public std::false_type { };

template <class T>
struct IsTensor<
  T,
  typename std::enable_if<
    std::is_base_of<
      TensorBase<typename T::value_type,
                 typename T::StorageType,
                 T>,
      T>::value>::type>
  : public std::true_type { };

template <class T, class Enable=void>
struct IsTensorPtr : public std::false_type { };

template <class T>
struct IsTensorPtr<
  TensorPtr<T>,
  typename std::enable_if<IsTensor<T>::value>::type>
  : public std::true_type { };

}  // namespaces

#include <thpp/TensorBase-inl.h>

#endif /* THPP_TENSORBASE_H_ */
