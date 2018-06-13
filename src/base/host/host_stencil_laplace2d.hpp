#ifndef ROCALUTION_HOST_STENCIL_LAPLACE2D_HPP_
#define ROCALUTION_HOST_STENCIL_LAPLACE2D_HPP_

#include "../base_vector.hpp"
#include "../base_stencil.hpp"
#include "../stencil_types.hpp"

namespace rocalution {

template <typename ValueType>
class HostStencilLaplace2D : public HostStencil<ValueType> {

public:

  HostStencilLaplace2D();
  HostStencilLaplace2D(const Rocalution_Backend_Descriptor local_backend);
  virtual ~HostStencilLaplace2D();

  virtual int get_nnz(void) const;
  virtual void Info(void) const;
  virtual unsigned int get_stencil_id(void) const { return  Laplace2D; }

 
  virtual void Apply(const BaseVector<ValueType> &in, BaseVector<ValueType> *out) const;
  virtual void ApplyAdd(const BaseVector<ValueType> &in, const ValueType scalar,
                        BaseVector<ValueType> *out) const;

private:

  friend class BaseVector<ValueType>;
  friend class HostVector<ValueType>;

  //  friend class HIPAcceleratorStencilLaplace2D<ValueType>;

};


}

#endif // ROCALUTION_HOST_STENCIL_LAPLACE2D_HPP_
