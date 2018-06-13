#include "../../utils/def.hpp"
#include "hip_matrix_csr.hpp"
#include "hip_matrix_coo.hpp"
#include "hip_vector.hpp"
#include "../host/host_matrix_coo.hpp"
#include "../base_matrix.hpp"
#include "../base_vector.hpp"
#include "../backend_manager.hpp"
#include "../../utils/log.hpp"
#include "hip_utils.hpp"
#include "hip_kernels_general.hpp"
#include "hip_kernels_coo.hpp"
#include "hip_allocate_free.hpp"
#include "hip_sparse.hpp"
#include "../matrix_formats_ind.hpp"

#include <algorithm>

#include <hip/hip_runtime.h>

namespace rocalution {

template <typename ValueType>
HIPAcceleratorMatrixCOO<ValueType>::HIPAcceleratorMatrixCOO() {

  // no default constructors
  LOG_INFO("no default constructor");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
HIPAcceleratorMatrixCOO<ValueType>::HIPAcceleratorMatrixCOO(const Rocalution_Backend_Descriptor local_backend) {

  LOG_DEBUG(this, "HIPAcceleratorMatrixCOO::HIPAcceleratorMatrixCOO()",
            "constructor with local_backend");

  this->mat_.row = NULL;  
  this->mat_.col = NULL;  
  this->mat_.val = NULL;
  this->set_backend(local_backend); 

  this->mat_descr_ = 0;

  CHECK_HIP_ERROR(__FILE__, __LINE__);

  hipsparseStatus_t stat_t;
  
  stat_t = hipsparseCreateMatDescr(&this->mat_descr_);
  CHECK_HIPSPARSE_ERROR(stat_t, __FILE__, __LINE__);
  
  stat_t = hipsparseSetMatIndexBase(this->mat_descr_, HIPSPARSE_INDEX_BASE_ZERO);
  CHECK_HIPSPARSE_ERROR(stat_t, __FILE__, __LINE__);
  
  stat_t = hipsparseSetMatType(this->mat_descr_, HIPSPARSE_MATRIX_TYPE_GENERAL);
  CHECK_HIPSPARSE_ERROR(stat_t, __FILE__, __LINE__);

}


template <typename ValueType>
HIPAcceleratorMatrixCOO<ValueType>::~HIPAcceleratorMatrixCOO() {

  LOG_DEBUG(this, "HIPAcceleratorMatrixCOO::~HIPAcceleratorMatrixCOO()",
            "destructor");

  this->Clear();

  hipsparseStatus_t stat_t;

  stat_t = hipsparseDestroyMatDescr(this->mat_descr_);
  CHECK_HIPSPARSE_ERROR(stat_t, __FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::Info(void) const {

  LOG_INFO("HIPAcceleratorMatrixCOO<ValueType>");

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::AllocateCOO(const int nnz, const int nrow, const int ncol) {

  assert(nnz >= 0);
  assert(ncol >= 0);
  assert(nrow >= 0);

  if (this->get_nnz() > 0)
    this->Clear();

  if (nnz > 0) {

    allocate_hip(nnz, &this->mat_.row);
    allocate_hip(nnz, &this->mat_.col);
    allocate_hip(nnz, &this->mat_.val);
 
    set_to_zero_hip(this->local_backend_.HIP_block_size, 
                    nnz, this->mat_.row);
    set_to_zero_hip(this->local_backend_.HIP_block_size, 
                    nnz, this->mat_.col);
    set_to_zero_hip(this->local_backend_.HIP_block_size, 
                    nnz, this->mat_.val);

    this->nrow_ = nrow;
    this->ncol_ = ncol;
    this->nnz_  = nnz;

  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::SetDataPtrCOO(int **row, int **col, ValueType **val,
                                                       const int nnz, const int nrow, const int ncol) {

  assert(*row != NULL);
  assert(*col != NULL);
  assert(*val != NULL);
  assert(nnz > 0);
  assert(nrow > 0);
  assert(ncol > 0);

  this->Clear();

  this->nrow_ = nrow;
  this->ncol_ = ncol;
  this->nnz_  = nnz;

  hipDeviceSynchronize();

  this->mat_.row = *row;
  this->mat_.col = *col;
  this->mat_.val = *val;

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::LeaveDataPtrCOO(int **row, int **col, ValueType **val) {

  assert(this->get_nrow() > 0);
  assert(this->get_ncol() > 0);
  assert(this->get_nnz() > 0);

  hipDeviceSynchronize();

  // see free_host function for details
  *row = this->mat_.row;
  *col = this->mat_.col;
  *val = this->mat_.val;

  this->mat_.row = NULL;
  this->mat_.col = NULL;
  this->mat_.val = NULL;

  this->nrow_ = 0;
  this->ncol_ = 0;
  this->nnz_  = 0;

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::Clear() {

  if (this->get_nnz() > 0) {

    free_hip(&this->mat_.row);
    free_hip(&this->mat_.col);
    free_hip(&this->mat_.val);

    this->nrow_ = 0;
    this->ncol_ = 0;
    this->nnz_  = 0;

  }


}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyFromHost(const HostMatrix<ValueType> &src) {

  const HostMatrixCOO<ValueType> *cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == src.get_mat_format());

  // CPU to HIP copy
  if ((cast_mat = dynamic_cast<const HostMatrixCOO<ValueType>*> (&src)) != NULL) {
    
  if (this->get_nnz() == 0)
    this->AllocateCOO(src.get_nnz(), src.get_nrow(), src.get_ncol() );

  if (this->get_nnz() > 0) {

      assert(this->get_nnz()  == src.get_nnz());
      assert(this->get_nrow()  == src.get_nrow());
      assert(this->get_ncol()  == src.get_ncol());
      
      hipMemcpy(this->mat_.row,     // dst
                 cast_mat->mat_.row, // src
                 (this->get_nnz())*sizeof(int), // size
                 hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(this->mat_.col,     // dst
                 cast_mat->mat_.col, // src
                 this->get_nnz()*sizeof(int), // size
                 hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(this->mat_.val,     // dst
                 cast_mat->mat_.val, // src
                 this->get_nnz()*sizeof(ValueType), // size
                 hipMemcpyHostToDevice);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }
    
  } else {
    
    LOG_INFO("Error unsupported HIP matrix type");
    this->Info();
    src.Info();
    FATAL_ERROR(__FILE__, __LINE__);
    
  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyToHost(HostMatrix<ValueType> *dst) const {

  HostMatrixCOO<ValueType> *cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == dst->get_mat_format());

  // HIP to CPU copy
  if ((cast_mat = dynamic_cast<HostMatrixCOO<ValueType>*> (dst)) != NULL) {

    cast_mat->set_backend(this->local_backend_);   

  if (dst->get_nnz() == 0)
    cast_mat->AllocateCOO(this->get_nnz(), this->get_nrow(), this->get_ncol() );

  if (this->get_nnz() > 0) {

      assert(this->get_nnz()  == dst->get_nnz());
      assert(this->get_nrow() == dst->get_nrow());
      assert(this->get_ncol() == dst->get_ncol());
      
      hipMemcpy(cast_mat->mat_.row, // dst
                 this->mat_.row,     // src
                 this->get_nnz()*sizeof(int), // size           
                 hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(cast_mat->mat_.col, // dst
                 this->mat_.col,     // src
                 this->get_nnz()*sizeof(int), // size
                 hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(cast_mat->mat_.val, // dst
                 this->mat_.val,     // src
                 this->get_nnz()*sizeof(ValueType), // size
                 hipMemcpyDeviceToHost);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }
    
  } else {
    
    LOG_INFO("Error unsupported HIP matrix type");
    this->Info();
    dst->Info();
    FATAL_ERROR(__FILE__, __LINE__);
    
  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyFrom(const BaseMatrix<ValueType> &src) {

  const HIPAcceleratorMatrixCOO<ValueType> *hip_cast_mat;
  const HostMatrix<ValueType> *host_cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == src.get_mat_format());

  // HIP to HIP copy
  if ((hip_cast_mat = dynamic_cast<const HIPAcceleratorMatrixCOO<ValueType>*> (&src)) != NULL) {
    
  if (this->get_nnz() == 0)
    this->AllocateCOO(src.get_nnz(), src.get_nrow(), src.get_ncol() );

    assert(this->get_nnz()  == src.get_nnz());
    assert(this->get_nrow() == src.get_nrow());
    assert(this->get_ncol() == src.get_ncol());

    if (this->get_nnz() > 0) {

      hipMemcpy(this->mat_.row,         // dst
                 hip_cast_mat->mat_.row, // src
                 (this->get_nnz())*sizeof(int), // size
                 hipMemcpyDeviceToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(this->mat_.col,         // dst
                 hip_cast_mat->mat_.col, // src
                 this->get_nnz()*sizeof(int), // size
                 hipMemcpyDeviceToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(this->mat_.val,         // dst
                 hip_cast_mat->mat_.val, // src
                 this->get_nnz()*sizeof(ValueType), // size
                 hipMemcpyDeviceToDevice);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }

  } else {

    //CPU to HIP
    if ((host_cast_mat = dynamic_cast<const HostMatrix<ValueType>*> (&src)) != NULL) {
      
      this->CopyFromHost(*host_cast_mat);
      
    } else {
      
      LOG_INFO("Error unsupported HIP matrix type");
      this->Info();
      src.Info();
      FATAL_ERROR(__FILE__, __LINE__);
      
    }
    
  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyTo(BaseMatrix<ValueType> *dst) const {

  HIPAcceleratorMatrixCOO<ValueType> *hip_cast_mat;
  HostMatrix<ValueType> *host_cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == dst->get_mat_format());

  // HIP to HIP copy
  if ((hip_cast_mat = dynamic_cast<HIPAcceleratorMatrixCOO<ValueType>*> (dst)) != NULL) {

    hip_cast_mat->set_backend(this->local_backend_);       

  if (this->get_nnz() == 0)
    hip_cast_mat->AllocateCOO(dst->get_nnz(), dst->get_nrow(), dst->get_ncol() );

    assert(this->get_nnz()  == dst->get_nnz());
    assert(this->get_nrow() == dst->get_nrow());
    assert(this->get_ncol() == dst->get_ncol());

    if (this->get_nnz() > 0) {

      hipMemcpy(hip_cast_mat->mat_.row, // dst
                 this->mat_.row,         // src
                 (this->get_nnz())*sizeof(int), // size
                 hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(hip_cast_mat->mat_.col, // dst
                 this->mat_.col,         // src
                 this->get_nnz()*sizeof(int), // size
                 hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(hip_cast_mat->mat_.val, // dst
                 this->mat_.val,         // src
                 this->get_nnz()*sizeof(ValueType), // size
                 hipMemcpyDeviceToHost);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }
    
  } else {

    //HIP to CPU
    if ((host_cast_mat = dynamic_cast<HostMatrix<ValueType>*> (dst)) != NULL) {
      
      this->CopyToHost(host_cast_mat);

    } else {
      
      LOG_INFO("Error unsupported HIP matrix type");
      this->Info();
      dst->Info();
      FATAL_ERROR(__FILE__, __LINE__);
      
    }

  }


}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyFromHostAsync(const HostMatrix<ValueType> &src) {

  const HostMatrixCOO<ValueType> *cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == src.get_mat_format());

  // CPU to HIP copy
  if ((cast_mat = dynamic_cast<const HostMatrixCOO<ValueType>*> (&src)) != NULL) {
    
  if (this->get_nnz() == 0)
    this->AllocateCOO(src.get_nnz(), src.get_nrow(), src.get_ncol() );

  if (this->get_nnz() > 0) {

      assert(this->get_nnz()  == src.get_nnz());
      assert(this->get_nrow()  == src.get_nrow());
      assert(this->get_ncol()  == src.get_ncol());
      
      hipMemcpyAsync(this->mat_.row,     // dst
                      cast_mat->mat_.row, // src
                      (this->get_nnz())*sizeof(int), // size
                      hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpyAsync(this->mat_.col,     // dst
                      cast_mat->mat_.col, // src
                      this->get_nnz()*sizeof(int), // size
                      hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpyAsync(this->mat_.val,     // dst
                      cast_mat->mat_.val, // src
                      this->get_nnz()*sizeof(ValueType), // size
                      hipMemcpyHostToDevice);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }
    
  } else {
    
    LOG_INFO("Error unsupported HIP matrix type");
    this->Info();
    src.Info();
    FATAL_ERROR(__FILE__, __LINE__);
    
  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyToHostAsync(HostMatrix<ValueType> *dst) const {

  HostMatrixCOO<ValueType> *cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == dst->get_mat_format());

  // HIP to CPU copy
  if ((cast_mat = dynamic_cast<HostMatrixCOO<ValueType>*> (dst)) != NULL) {

    cast_mat->set_backend(this->local_backend_);   

  if (dst->get_nnz() == 0)
    cast_mat->AllocateCOO(this->get_nnz(), this->get_nrow(), this->get_ncol() );

  if (this->get_nnz() > 0) {

      assert(this->get_nnz()  == dst->get_nnz());
      assert(this->get_nrow() == dst->get_nrow());
      assert(this->get_ncol() == dst->get_ncol());
      
      hipMemcpyAsync(cast_mat->mat_.row, // dst
                      this->mat_.row,     // src
                      this->get_nnz()*sizeof(int), // size           
                      hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpyAsync(cast_mat->mat_.col, // dst
                      this->mat_.col,     // src
                      this->get_nnz()*sizeof(int), // size
                      hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpyAsync(cast_mat->mat_.val, // dst
                      this->mat_.val,     // src
                      this->get_nnz()*sizeof(ValueType), // size
                      hipMemcpyDeviceToHost);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }
    
  } else {
    
    LOG_INFO("Error unsupported HIP matrix type");
    this->Info();
    dst->Info();
    FATAL_ERROR(__FILE__, __LINE__);
    
  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyFromAsync(const BaseMatrix<ValueType> &src) {

  const HIPAcceleratorMatrixCOO<ValueType> *hip_cast_mat;
  const HostMatrix<ValueType> *host_cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == src.get_mat_format());

  // HIP to HIP copy
  if ((hip_cast_mat = dynamic_cast<const HIPAcceleratorMatrixCOO<ValueType>*> (&src)) != NULL) {
    
  if (this->get_nnz() == 0)
    this->AllocateCOO(src.get_nnz(), src.get_nrow(), src.get_ncol() );

    assert(this->get_nnz()  == src.get_nnz());
    assert(this->get_nrow() == src.get_nrow());
    assert(this->get_ncol() == src.get_ncol());

    if (this->get_nnz() > 0) {

      hipMemcpy(this->mat_.row,         // dst
                 hip_cast_mat->mat_.row, // src
                 (this->get_nnz())*sizeof(int), // size
                 hipMemcpyDeviceToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(this->mat_.col,         // dst
                 hip_cast_mat->mat_.col, // src
                 this->get_nnz()*sizeof(int), // size
                 hipMemcpyDeviceToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(this->mat_.val,         // dst
                 hip_cast_mat->mat_.val, // src
                 this->get_nnz()*sizeof(ValueType), // size
                 hipMemcpyDeviceToDevice);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }

  } else {

    //CPU to HIP
    if ((host_cast_mat = dynamic_cast<const HostMatrix<ValueType>*> (&src)) != NULL) {
      
      this->CopyFromHostAsync(*host_cast_mat);
      
    } else {
      
      LOG_INFO("Error unsupported HIP matrix type");
      this->Info();
      src.Info();
      FATAL_ERROR(__FILE__, __LINE__);
      
    }
    
  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyToAsync(BaseMatrix<ValueType> *dst) const {

  HIPAcceleratorMatrixCOO<ValueType> *hip_cast_mat;
  HostMatrix<ValueType> *host_cast_mat;

  // copy only in the same format
  assert(this->get_mat_format() == dst->get_mat_format());

  // HIP to HIP copy
  if ((hip_cast_mat = dynamic_cast<HIPAcceleratorMatrixCOO<ValueType>*> (dst)) != NULL) {

    hip_cast_mat->set_backend(this->local_backend_);       

  if (this->get_nnz() == 0)
    hip_cast_mat->AllocateCOO(dst->get_nnz(), dst->get_nrow(), dst->get_ncol() );

    assert(this->get_nnz()  == dst->get_nnz());
    assert(this->get_nrow() == dst->get_nrow());
    assert(this->get_ncol() == dst->get_ncol());

    if (this->get_nnz() > 0) {

      hipMemcpy(hip_cast_mat->mat_.row, // dst
                 this->mat_.row,         // src
                 (this->get_nnz())*sizeof(int), // size
                 hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(hip_cast_mat->mat_.col, // dst
                 this->mat_.col,         // src
                 this->get_nnz()*sizeof(int), // size
                 hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
      
      hipMemcpy(hip_cast_mat->mat_.val, // dst
                 this->mat_.val,         // src
                 this->get_nnz()*sizeof(ValueType), // size
                 hipMemcpyDeviceToHost);    
      CHECK_HIP_ERROR(__FILE__, __LINE__);     
    }
    
  } else {

    //HIP to CPU
    if ((host_cast_mat = dynamic_cast<HostMatrix<ValueType>*> (dst)) != NULL) {
      
      this->CopyToHostAsync(host_cast_mat);

    } else {
      
      LOG_INFO("Error unsupported HIP matrix type");
      this->Info();
      dst->Info();
      FATAL_ERROR(__FILE__, __LINE__);
      
    }

  }


}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyFromCOO(const int *row, const int *col, const ValueType *val) {

  // assert CSR format
  assert(this->get_mat_format() == COO);

  if (this->get_nnz() > 0) {

    assert(this->nrow_ > 0);
    assert(this->ncol_ > 0);

    hipMemcpy(this->mat_.row,              // dst
               row,                         // src
               this->get_nnz()*sizeof(int), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    hipMemcpy(this->mat_.col,              // dst
               col,                         // src
               this->get_nnz()*sizeof(int), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    hipMemcpy(this->mat_.val,                    // dst
               val,                               // src
               this->get_nnz()*sizeof(ValueType), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::CopyToCOO(int *row, int *col, ValueType *val) const {

  // assert CSR format
  assert(this->get_mat_format() == COO);

  if (this->get_nnz() > 0) {

    assert(this->nrow_ > 0);
    assert(this->ncol_ > 0);

    hipMemcpy(row,                         // dst
               this->mat_.row,              // src
               this->get_nnz()*sizeof(int), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    hipMemcpy(col,                         // dst
               this->mat_.col,              // src
               this->get_nnz()*sizeof(int), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    hipMemcpy(val,                               // dst
               this->mat_.val,                    // src
               this->get_nnz()*sizeof(ValueType), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
bool HIPAcceleratorMatrixCOO<ValueType>::ConvertFrom(const BaseMatrix<ValueType> &mat) {

  this->Clear();

  // empty matrix is empty matrix
  if (mat.get_nnz() == 0)
    return true;

  const HIPAcceleratorMatrixCOO<ValueType> *cast_mat_coo;

  if ((cast_mat_coo = dynamic_cast<const HIPAcceleratorMatrixCOO<ValueType>*> (&mat)) != NULL) {

      this->CopyFrom(*cast_mat_coo);
      return true;

  }

  const HIPAcceleratorMatrixCSR<ValueType> *cast_mat_csr;

  if ((cast_mat_csr = dynamic_cast<const HIPAcceleratorMatrixCSR<ValueType>*> (&mat)) != NULL) {

    this->Clear();

    int nrow = cast_mat_csr->get_nrow();
    int ncol = cast_mat_csr->get_ncol();
    int nnz  = cast_mat_csr->get_nnz();

    assert(nrow > 0);
    assert(ncol > 0);
    assert(nnz > 0);

    this->AllocateCOO(nnz, nrow, ncol);

    hipsparseStatus_t stat_t;

    hipMemcpyAsync(this->mat_.col, cast_mat_csr->mat_.col, nnz*sizeof(int), hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    hipMemcpyAsync(this->mat_.val, cast_mat_csr->mat_.val, nnz*sizeof(ValueType), hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    stat_t = hipsparseXcsr2coo(HIPSPARSE_HANDLE(this->local_backend_.HIP_sparse_handle),
                               cast_mat_csr->mat_.row_offset,
                               nnz,
                               nrow,
                               this->mat_.row,
                               HIPSPARSE_INDEX_BASE_ZERO);
    CHECK_HIPSPARSE_ERROR(stat_t, __FILE__, __LINE__);

    // Sync memcopy
    hipDeviceSynchronize();

    return true;

  }

  return false;

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::Apply(const BaseVector<ValueType> &in, BaseVector<ValueType> *out) const {

  if (this->get_nnz() > 0) {

    assert(in.  get_size() >= 0);
    assert(out->get_size() >= 0);
    assert(in.  get_size() == this->get_ncol());
    assert(out->get_size() == this->get_nrow());

    const HIPAcceleratorVector<ValueType> *cast_in = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&in);
    HIPAcceleratorVector<ValueType> *cast_out      = dynamic_cast<      HIPAcceleratorVector<ValueType>*> (out);

    assert(cast_in != NULL);
    assert(cast_out!= NULL);

    ValueType alpha = 1.0;
    ValueType beta = 0.0;

    hipsparseStatus_t stat_t;
    stat_t = hipsparseTcoomv(HIPSPARSE_HANDLE(this->local_backend_.HIP_sparse_handle),
                             HIPSPARSE_OPERATION_NON_TRANSPOSE,
                             this->get_nrow(), this->get_ncol(), this->get_nnz(), &alpha,
                             this->mat_descr_,
                             this->mat_.val, this->mat_.row, this->mat_.col,
                             cast_in->vec_, &beta,
                             cast_out->vec_);
    CHECK_HIPSPARSE_ERROR(stat_t, __FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorMatrixCOO<ValueType>::ApplyAdd(const BaseVector<ValueType> &in, const ValueType scalar,
                                                  BaseVector<ValueType> *out) const {

  if (this->get_nnz() > 0) {

    assert(in.  get_size() >= 0);
    assert(out->get_size() >= 0);
    assert(in.  get_size() == this->get_ncol());
    assert(out->get_size() == this->get_nrow());

    const HIPAcceleratorVector<ValueType> *cast_in = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&in);
    HIPAcceleratorVector<ValueType> *cast_out      = dynamic_cast<      HIPAcceleratorVector<ValueType>*> (out);

    assert(cast_in != NULL);
    assert(cast_out!= NULL);

    ValueType beta = 1.0;

    hipsparseStatus_t stat_t;
    stat_t = hipsparseTcoomv(HIPSPARSE_HANDLE(this->local_backend_.HIP_sparse_handle),
                             HIPSPARSE_OPERATION_NON_TRANSPOSE,
                             this->get_nrow(), this->get_ncol(), this->get_nnz(), &scalar,
                             this->mat_descr_,
                             this->mat_.val, this->mat_.row, this->mat_.col,
                             cast_in->vec_, &beta,
                             cast_out->vec_);
    CHECK_HIPSPARSE_ERROR(stat_t, __FILE__, __LINE__);

  }

}

template <typename ValueType>
bool HIPAcceleratorMatrixCOO<ValueType>::Permute(const BaseVector<int> &permutation) {

  // symmetric permutation only
  assert(permutation.get_size() == this->get_nrow());
  assert(permutation.get_size() == this->get_ncol());

  if (this->get_nnz() > 0) {

    const HIPAcceleratorVector<int> *cast_perm = dynamic_cast<const HIPAcceleratorVector<int>*> (&permutation);
    assert(cast_perm != NULL);

    HIPAcceleratorMatrixCOO<ValueType> src(this->local_backend_);
    src.AllocateCOO(this->get_nnz(), this->get_nrow(), this->get_ncol());
    src.CopyFrom(*this);

    int nnz = this->get_nnz();
    int s = nnz;
    int k = (nnz/this->local_backend_.HIP_block_size)/this->local_backend_.HIP_max_threads + 1;
    if (k > 1) s = nnz / k;

    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(s / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_coo_permute<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       nnz,
                       src.mat_.row, src.mat_.col,
                       cast_perm->vec_,
                       this->mat_.row, this->mat_.col);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

  return true;

}

template <typename ValueType>
bool HIPAcceleratorMatrixCOO<ValueType>::PermuteBackward(const BaseVector<int> &permutation) {

  // symmetric permutation only
  assert(permutation.get_size() == this->get_nrow());
  assert(permutation.get_size() == this->get_ncol());

  if (this->get_nnz() > 0) {

    const HIPAcceleratorVector<int> *cast_perm = dynamic_cast<const HIPAcceleratorVector<int>*> (&permutation);
    assert(cast_perm != NULL);

    int *pb = NULL;
    allocate_hip(this->get_nrow(), &pb);

    int n = this->get_nrow();
    dim3 BlockSize1(this->local_backend_.HIP_block_size);
    dim3 GridSize1(n / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_reverse_index<int>),
                       GridSize1, BlockSize1, 0, 0,
                       n, cast_perm->vec_, pb);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    HIPAcceleratorMatrixCOO<ValueType> src(this->local_backend_);
    src.AllocateCOO(this->get_nnz(), this->get_nrow(), this->get_ncol());
    src.CopyFrom(*this);

    int nnz = this->get_nnz();
    int s = nnz;
    int k = (nnz/this->local_backend_.HIP_block_size)/this->local_backend_.HIP_max_threads + 1;
    if (k > 1) s = nnz / k;

    dim3 BlockSize2(this->local_backend_.HIP_block_size);
    dim3 GridSize2(s / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_coo_permute<ValueType, int>),
                       GridSize2, BlockSize2, 0, 0,
                       nnz, src.mat_.row, src.mat_.col, pb,
                       this->mat_.row, this->mat_.col);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

    free_hip(&pb);

  }

  return true;

}


template class HIPAcceleratorMatrixCOO<double>;
template class HIPAcceleratorMatrixCOO<float>;
#ifdef SUPPORT_COMPLEX
template class HIPAcceleratorMatrixCOO<std::complex<double> >;
template class HIPAcceleratorMatrixCOO<std::complex<float> >;
#endif

}
