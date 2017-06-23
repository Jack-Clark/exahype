/**
 * This file is part of the ExaHyPE project.
 * Copyright (c) 2016  http://exahype.eu
 * All rights reserved.
 *
 * The project has received funding from the European Union's Horizon 
 * 2020 research and innovation programme under grant agreement
 * No 671698. For copyrights and licensing, please consult the webpage.
 *
 * Released under the BSD 3 Open Source License.
 * For the full license text, see LICENSE.txt
 **/
 
#ifndef _EXAHYPE_SOLVERS_BASISSOLVER_H
#define _EXAHYPE_SOLVERS_BASISSOLVER_H

namespace exahype {
namespace solvers {

class UserSolverInterface;
class UserADERDGSolverInterface;
class UserFiniteVolumesSolverInterface;

} /* namespace solvers */
} /* namespace exahype */



/**
 * The Basis API for User solvers, purely virtual. New from 2017-05-14.
 * Cf. https://gitlab.lrz.de/exahype/ExaHyPE-Engine/issues/143
 * 
 * This is for a template-free glue code (Abstract*Solver).
 * 
 * Direct classes which inherit UserSolverInterface:
 *   1) ADERDGSolver
 *   3) FVSolver
 *
 * TODO: The UseAdjustSolution() user functions should be unified accross
 *   FV/ADERDG solvers and also be added here.
 **/
class exahype::solvers::UserSolverInterface {
public:
  virtual ~UserSolverInterface() {};

 /**
  * @defgroup Theoretically-Constexpr-Getters
  */
  ///@{
  // Read off the constexpr's in the Abstract*Solver
  virtual int constexpr_getNumberOfVariables()  const = 0;
  virtual int constexpr_getNumberOfParameters() const = 0;
  virtual double constexpr_getCFLNumber()       const = 0;
  ///@}

 /**
  * @defgroup Guards
  */
  ///@{

  /**
   * Guard to enable conservative fluxes in the User PDE,
   * ie terms $\nabla F(Q)$.
   **/
  virtual bool useConservativeFlux()       const = 0;
  
  /**
   * Guard to enable non conservative contributions in the User PDE,
   * ie. terms $B(Q) \nabla Q$.
   **/
  virtual bool useNonConservativeProduct() const = 0;
  
  /**
   * Guard to enable algebaric source terms in the User PDE,
   * ie. terms $S(Q)$ typically written on the right hand side of the
   * equation.
   **/
  virtual bool useAlgebraicSource()                 const = 0;
  
  /**
   * Guard to enable dirac point source terms in the User PDE.
   **/
  virtual bool usePointSource()            const = 0;
  ///@}
  
 /**
  * @defgroup User PDE
  */
  ///@{
  /**
   * Compute a pointSource contribution.
   * 
   * @TODO: Document me, please.
   **/
  virtual void pointSource(const double* const x,const double t,const double dt, double* forceVector, double* x0) = 0;

  /**
   * Compute the Algebraic Sourceterms.
   * 
   * You may want to overwrite this with your PDE Source (algebraic RHS contributions).
   * However, in all schemes we have so far, the source-type contributions are
   * collected with non-conservative contributions into a fusedSource, see the
   * fusedSource method. From the kernels given with ExaHyPE, only the fusedSource
   * is called and there is a default implementation for the fusedSource calling
   * again seperately the nonConservativeProduct function and the algebraicSource
   * function.
   *
   * \param[in]    Q the conserved variables (and parameters) associated with a quadrature point
   *                 as C array (already allocated).
   * \param[inout] S the source point as C array (already allocated).
   */
  virtual void algebraicSource(const double* const Q,double* S) = 0;

  /**
   * Compute the fused Source.
   * 
   * The fused source is the sum $S(Q) - B(Q)\nabla Q$ stemming
   * from the algebraicSource and the nonConservativeProduct functions.
   * 
   * In most ExaHyPE kernels, this function is the only one called and
   * there is an adapter calling the old functions if neccessary.
   **/
  virtual void fusedSource(const double* const Q, const double* const gradQ, double* S) = 0;
  
  /**
   * Compute the nonconservative term $B(Q) \nabla Q$.
   * 
   * This function shall return a vector BgradQ which holds the result
   * of the full term. To do so, it gets the vector Q and the matrix
   * gradQ which holds the derivative of Q in each spatial direction.
   * Currently, the gradQ is a continous storage and users can use the
   * kernels::idx2 class in order to compute the positions inside gradQ.
   *
   * @TODO: Check if the following is still right:
   * 
   * !!! Warning: BgradQ is a vector of size NumberOfVariables if you
   * use the ADER-DG kernels for nonlinear PDEs. If you use
   * the kernels for linear PDEs, it is a tensor with dimensions
   * Dim x NumberOfVariables.
   * 
   * \param[in]   Q   the vector of unknowns at the given position
   * \param[in]   gradQ   the gradients of the vector of unknowns,
   *                  stored in a linearized array.
   * \param[inout]  The vector BgradQ (extends nVar), already allocated. 
   *
   **/
  virtual void nonConservativeProduct(const double* const Q,const double* const gradQ,double* BgradQ) = 0;
  
  /**
   * Compute the nonconservative matrix B(Q).
   * 
   * The function shall compute <i>almost</i> the same as nonConservativeProduct.
   * Indeed, we have it as some Riemann solvers can do a quicker computation with
   * the full matrix. If you don't provide it, the toolkit will typically generate
   * glue code which allows computing the coefficientMatrix directly from the
   * nonConservativeProduct function.
   * 
   * \param[in]   Q the vector of unknowns at the given position
   * \param[in]   d the normal index (nonzero), indicating the spatial direction
   * \param[inout]  The Matrix nVar*nVar, already allocated and flattened.
   *
   **/
  virtual void coefficientMatrix(const double* const Q,const int d,double* Bn) = 0;
  

  /**
   * Compute the conserved flux.
   * 
   * \param[in]  Q the conserved variabels (and parameters) associated with a
   *               quadrature point as C array.
   * \param[inout] F a C array with shape [nDim][nVars]. That is, this is an C list
   *               holding pointers to actual lists. Thus, the storage may be noncontinous.
   *               In any case, the storage has already been allocated.
   **/
  virtual void flux(const double* const Q,double** F) = 0;
  
  ///@}
};
 // UserSolverInterface

class exahype::solvers::UserADERDGSolverInterface : public exahype::solvers::UserSolverInterface {
public:
  virtual ~UserADERDGSolverInterface() {};

  virtual int constexpr_getOrder()  const  = 0;
};

class exahype::solvers::UserFiniteVolumesSolverInterface : public exahype::solvers::UserSolverInterface {
public:
  virtual ~UserFiniteVolumesSolverInterface() {};

  virtual int constexpr_getPatchSize()  const  = 0;
  virtual int constexpr_getGhostLayerWidth() const  = 0;
};

#endif /* _EXAHYPE_SOLVERS_BASISSOLVER_H */
