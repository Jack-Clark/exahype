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

#ifndef ADAPTIVEMESHREFINEMENT_H_
#define ADAPTIVEMESHREFINEMENT_H_

#include "peano/utils/Globals.h"

#include "exahype/Cell.h"

#include "multiscalelinkedcell/HangingVertexBookkeeper.h"

namespace exahype {

namespace amr {
  /**
   * Per coordinate direction xi, count the number of shifts
   * of step size \p childSize(xi) necessary to
   * reach \p childOffset from \p parentOffset.
   *
   * \param[in] childOffset  Offset of a child cell.
   * \param[in] childSize    Size of the child cell.
   * \param[in] parentOffset Offset of the parent cell.
   *
   * \see getSubfaceIndex
   */
  tarch::la::Vector<DIMENSIONS,int> computeSubcellIndex(
        const tarch::la::Vector<DIMENSIONS,double>& childOffset,
        const tarch::la::Vector<DIMENSIONS,double>& childSize,
        const tarch::la::Vector<DIMENSIONS,double>& parentOffset);

  /**
   * Collect all the element with index!=d
   * from \p subcellIndex.
   *
   * \see getSubcellIndex
   */
  tarch::la::Vector<DIMENSIONS-1,int> getSubfaceIndex(
        const tarch::la::Vector<DIMENSIONS, int>& subcellIndex,
        const int d);

  /**
   * Per coordinate direction xi, check if
   * subcellIndex[xi] is either 0 or 3^levelDelta - 1.
   * If this is the case for at least one subcellIndex[xi]
   * return true. Otherwise return false.
   */
  bool onBoundaryOfParent(
      const tarch::la::Vector<DIMENSIONS, int>& subcellIndex,
      const int levelDelta);

  /**
   * Returns  AugmentationControl::::NextToCell if the cell has a neighbour of
   * type exahype::records::ADERDGCellDescription:Cell,
   * exahype::solvers::Solver::NextToAncestor if the cell has a neighbour of
   * type exahype::records::ADERDGCellDescription:Ancestor or
   * exahype::records::ADERDGCellDescription::EmptyAncestor.
   * Is both the case, this function returns
   * AugmentationControl::::NextToCellOrAncestor.
   * If none of the previous is the case, this function returns
   * AugmentationControl::Default.
   */
  template<class CellDescription,class CellDescriptionsHeap>
  exahype::solvers::Solver::AugmentationControl
  augmentationCriterion(
      const int solverNumber,
      const typename CellDescription::Type type,
      const int level,
      const tarch::la::Vector<THREE_POWER_D, int>&
      neighbourCellDescriptionsIndices);

  /*
   * Change the erasing request to a change to descendant request if the coarse grid Cell
   * has children (of type Descendant).
   * Rationale: We cannot directly erase a Cell that has children (of type Descendant).
   *
   * Further, reset the deaugmenting request if a coarse grid Descendant has children
   * (of type Descendant). Rationale:
   * We cannot erase a coarse grid cell that has children (of type Descendant)
   * before erasing the children.
   *
   * \note This method should be called when we enter a fine grid cell,
   * coarseGridCellDescription is a cell description associated with
   * the coarse grid cell.
   *
   * \note A more sophisticated procedure has to performed for the refinement event
   * AugmentationRequested. We need to use the taversal's descend event to handle
   * this event. We thus do not rely on fineGridCell.isRefined() in the previous enterCell event
   * to check if we need to reset the deaugmenting request.
   */
  template<class CellDescription>
  void resetErasingOrDeaugmentingRequestIfParent(CellDescription& coarseGridCellDescription);

  /**
   * Determine the position of a Cell or Ancestor with respect
   * to a parent of type Ancestor.
   * The return values subcellPosition.parentCellDescriptionsIndex
   * and subcellPosition.parentElement only
   * hold valid indices >= 0 if we have found a parent of type Ancestor
   * and the cell description itself is of type Cell or Ancestor or EmptyAncestor.
   *
   * Otherwise, subcellPosition.parentIndex holds the value
   * multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex,
   * subcellPosition.parentElement holds the value exahype::solver::Solvers::NotFound,
   * and subcellPosition.subcellIndex holds undefined values.
   * This applies to the case where the parent index of the
   * Cell or Ancestor is not a valid index and further to
   * the case where we have found a parent of type EmptyAncestor
   * as top most parent.
   *
   * This method is required for preparing cell description types
   * before sending the cell description away to a new worker.
   */
  template <class CellDescription,class CellDescriptionHeap>
  exahype::solvers::Solver::SubcellPosition
  computeSubcellPositionOfCellOrAncestorOrEmptyAncestor(const CellDescription& pChild);

  /**
   * Determine the position of a Cell or Ancestor with respect
   * to a parent of type Ancestor.
   * The return values subcellPosition.parentCellDescriptionsIndex
   * and subcellPosition.parentElement only
   * hold valid indices >= 0 if we have found a parent of type Ancestor
   * and the cell description itself is of type Cell or Ancestor.
   *
   * Otherwise, subcellPosition.parentIndex holds the value
   * multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex,
   * subcellPosition.parentElement holds the value exahype::solver::Solvers::NotFound,
   * and subcellPosition.subcellIndex holds undefined values.
   * This applies to the case where the parent index of the
   * Cell or Ancestor is not a valid index and further to
   * the case where we have found a parent of type EmptyAncestor
   * as top most parent.
   *
   * This method is required for the face data restriction, the
   * volume data restriction, and the FV volume data restriction.
   */
  template <class CellDescription,class CellDescriptionHeap>
  exahype::solvers::Solver::SubcellPosition
  computeSubcellPositionOfCellOrAncestor(const CellDescription& pChild);

  /**
   * Determine the position of a Descendant with respect
   * to a  Cell or Descendant that contains data, i.e.,
   * has at least one neighbour that is a real cell.
   *
   * \note This function only makes sense if the
   * Descendant has a valid parentIndex attribute.
   *
   * \note This method is less complicated that corresponding
   * method for computing the parent of
   * a Cell or Ancestor since a Descendant always(!) has
   * a parent of type Cell or Descendant.
   *
   * This method is required for the face data prolongation, the
   * volume data prolongation, and the FV volume data prolongation.
   *
   * \param topMost Set to true if you want to lookup the
   * rank-local top-most parent of the descendant.
   */
  template <class CellDescription,class CellDescriptionHeap, bool topMost>
  exahype::solvers::Solver::SubcellPosition
  computeSubcellPositionOfDescendant(const CellDescription& pChild);

}  // namespace amr
}  // namespace exahype


#include "AdaptiveMeshRefinement.cpph"


#endif /* ADAPTIVEMESHREFINEMENT_H_ */
