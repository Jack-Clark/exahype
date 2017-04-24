#include "exahype/plotters/ADERDG2CarpetHDF5.h"

#include <cstdlib>
#include <stdio.h>
#include <sstream>
#include <memory>


std::string exahype::plotters::ADERDG2CarpetHDF5::getIdentifier() {
	return std::string("experimental::CarpetHDF5");
}


#ifndef HDF5
/*************************************************************************************************
 * ADERDG2CarpetHDF5 Dummy implementation in case HDF5 support is skipped.
 *************************************************************************************************/

exahype::plotters::ADERDG2CarpetHDF5::ADERDG2CarpetHDF5(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing):
    Device(postProcessing) {
	printf("ERROR: Compile with HDF5, otherwise you cannot use the HDF5 plotter.\n");
	abort();
}

// all other methods are stubs
exahype::plotters::ADERDG2CarpetHDF5::~ADERDG2CarpetHDF5() {}
void exahype::plotters::ADERDG2CarpetHDF5::init(const std::string& filename, int orderPlusOne, int solverUnknowns, int writtenUnknowns, const std::string& select) {}
void exahype::plotters::ADERDG2CarpetHDF5::plotPatch(const int cellDescriptionsIndex, const int element) {}
void exahype::plotters::ADERDG2CarpetHDF5::plotPatch(const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch, double* u,double timeStamp) {}
void exahype::plotters::ADERDG2CarpetHDF5::startPlotting(double time) {}
void exahype::plotters::ADERDG2CarpetHDF5::finishPlotting() {}

#else
/*************************************************************************************************
 * The HDF5 Writer code actually starts here.
 * I embed the C++ class here mainly for laziness.
 *************************************************************************************************/

#include "kernels/KernelUtils.h" // indexing
#include "kernels/DGMatrices.h"
#include "peano/utils/Loop.h"
#include "exahype/solvers/ADERDGSolver.h"
#include "kernels/DGBasisFunctions.h"
#include "kernels/KernelUtils.h"
#include "tarch/logging/Log.h"
#include <sstream>

// HDF5 library, only available if HDF5 is on the path
#include "H5Cpp.h"

typedef tarch::la::Vector<DIMENSIONS, double> dvec;

// my small C++11 to_string-independent workaround.
template <typename T> std::string toString( T Number ) {
	std::ostringstream ss; ss << Number; return ss.str();
}

class exahype::plotters::ADERDG2CarpetHDF5Writer {
  tarch::logging::Log _log;

public:
  exahype::plotters::ADERDG2CarpetHDF5* device; // backlink
  H5::H5File *single_file, **seperate_files;
  H5::DataSpace patch_space, dtuple;

  bool oneFilePerTimestep, allUnknownsInOneFile;

  /**
   * cf. also the documentation in the ADERDG2CarpetHDF5.h
   * 
   * oneFilePerTimestep: You might want to have this to view the result during computation
   *     as HDF5 is very lazy at writing. Note that writing out data this form is not compilant
   *     with most CarpetHDF5 readers (ie. the visit reader). You must join seperate HDF5 files afterwards
   *     manually.
   * 
   * allUnknownsInOneFile: Write different fields in a single H5 combined file. Typically for Cactus
   *     as structure of arrays is to write each unknown in its own file (ie. one file per physical field).
   *
   **/
  ADERDG2CarpetHDF5Writer(exahype::plotters::ADERDG2CarpetHDF5* _device, bool oneFilePerTimestep_=false, bool allUnknownsInOneFile_=false) :
	_log("ADERDG2CarpetHDF5Writer"), device(_device),
	single_file(nullptr), seperate_files(nullptr),
	oneFilePerTimestep(oneFilePerTimestep_), allUnknownsInOneFile(allUnknownsInOneFile_) {}

  void init() {
	using namespace H5;
	
	// this is the dataspace describing how to write a patch/cell/component
	const int dims_rank = DIMENSIONS;
	hsize_t dims[dims_rank];
	std::fill_n(dims, DIMENSIONS, device->order+1);
	patch_space = DataSpace(dims_rank, dims);
	
	// this is just a vector of rank 1 with DIMENSIONS entries
	const int tupleDim_rank = 1;
	hsize_t tupleDim_len[] = {DIMENSIONS};
	dtuple = DataSpace(tupleDim_rank, tupleDim_len);
	
	if(!allUnknownsInOneFile) {
		seperate_files = new H5::H5File*[device->writtenUnknowns];
		std::fill_n(seperate_files, device->writtenUnknowns, nullptr);
	}

	// open file(s) initially, if neccessary
	if(!oneFilePerTimestep) {
		openH5(0);
	}
  }

  void writeBasicGroup(H5::H5File* file) {
	using namespace H5;
	Group* parameters = new Group(file->createGroup( "/Parameters and Global Attributes" ));
	
	int ranks = 1; // TODO: extend for MPI.
	
	Attribute nioprocs = parameters->createAttribute("nioprocs", PredType::NATIVE_INT, H5S_SCALAR);
	nioprocs.write(PredType::NATIVE_INT, &ranks);
	
	// Todo: add information how much files are printed (oneFilePerTimestep)
  }
  
  /**
   * We use a H5File** here in order to modify a passed pointer H5File* to a single
   * H5File.
   **/
  void openH5File(H5::H5File** file, std::string& filename) {
	using namespace H5;
	if(*file) {
		(*file)->close();
		delete *file;
		*file = nullptr;
	}
	
	logInfo("openH5File", "Opening File '"<< filename << "'");
	*file = new H5File( H5std_string(filename.c_str()), H5F_ACC_TRUNC  );
	(*file)->setComment("Created by ExaHyPE");
  }
  
  /**
   * Opens or switchs the currently active H5 file or the list of H5 files.
   * 
   **/
  void openH5(int iteration) {
	using namespace H5;
	
	std::string filename, suffix, prefix, sep("-");
	prefix = device->filename;
	suffix = (oneFilePerTimestep?(sep + "it" + toString(iteration)):"") + ".h5";
	if(allUnknownsInOneFile) {
		filename = prefix + suffix;
		openH5File(&single_file, filename);
	} else {
		for(int u=0; u < device->writtenUnknowns; u++) {
			char* writtenQuantityName = device->writtenQuantitiesNames[u];
			filename = prefix + sep + writtenQuantityName + suffix;
			openH5File(&seperate_files[u], filename);
		}
	}
  }
  
  void flushH5File(H5::H5File* file) {
	using namespace H5;
	if(file) {
		file->flush(H5F_SCOPE_GLOBAL);
	}
  }

  void startPlotting(double time, int iteration) {
	if(oneFilePerTimestep) {
		openH5(iteration);
	}
  }
  
  void finishPlotting() {
	// flush in any case, even when closing the file. Cf. http://stackoverflow.com/a/31301117
	if(allUnknownsInOneFile) {
		flushH5File(single_file);
	} else {
		for(int u=0; u < device->writtenUnknowns; u++) {
			flushH5File(seperate_files[u]);
		}
	}
  }

  /**
   * This is 2D and 3D, allows several unknowns, named fields and all that.
   **/
  void plotPatch(
      const dvec& offsetOfPatch, const dvec& sizeOfPatch,
      double* mappedCell, double timeStamp, int iteration, int component) {
	for(int writtenUnknown=0; writtenUnknown < device->writtenUnknowns; writtenUnknown++) {
		H5::H5File* target = allUnknownsInOneFile ? single_file : seperate_files[writtenUnknown];
		plotPatchForSingleUnknown(offsetOfPatch, sizeOfPatch, mappedCell, timeStamp, iteration, component, writtenUnknown, target);
	} // for writtenUnknown
  }

  void plotPatchForSingleUnknown(
      const dvec& offsetOfPatch, const dvec& sizeOfPatch,
      double* mappedCell, double timeStamp, int iteration, int component,
      int writtenUnknown, H5::H5File* target) {
        using namespace H5;
	const int order = device->order;

	// in CarpetHDF5, the field name *must* contain a "::"
	std::string name("ExaHyPE::");
	char* field_name = device->writtenQuantitiesNames[writtenUnknown];
	name += field_name ? field_name : "miserable-failure";
	
	char component_name[100];
	sprintf(component_name, "%s it=%d tl=0 m=0 rl=0 c=%d", name.c_str(), iteration, component);
	
	// 1) Compose a continous storage which is suitable.
	// 2) (Probably) Transpose the data.
	// TODO: I'm sure HDF5 provides a more performant way to interpret the different data layout.
	double *componentPatch = new double[device->singleFieldIdx->size];
	dfor(i,order+1) {
		#if DIMENSIONS==2
		componentPatch[device->singleFieldIdx->get(i(1),i(0))] = mappedCell[device->writtenCellIdx->get(i(0),i(1),writtenUnknown)];
		#else
		componentPatch[device->singleFieldIdx->get(i(2),i(1),i(0))] = mappedCell[device->writtenCellIdx->get(i(0),i(1),i(2),writtenUnknown)];
		#endif
	}
	
	DataSet table = target->createDataSet(component_name, PredType::NATIVE_FLOAT, patch_space);
	table.write(componentPatch, PredType::NATIVE_DOUBLE);
	
	delete[] componentPatch;
	
	// write all meta information about the table

	Attribute origin = table.createAttribute("origin", PredType::NATIVE_DOUBLE, dtuple);
	origin.write(PredType::NATIVE_DOUBLE, offsetOfPatch.data());
	
	dvec iorigin_data = 0.0;
	Attribute iorigin = table.createAttribute("iorigin", PredType::NATIVE_INT, dtuple);
	iorigin.write(PredType::NATIVE_INT, iorigin_data.data());
	
	int level_data = 0; // TODO: Read out real cell level
	Attribute level = table.createAttribute("level", PredType::NATIVE_INT, H5S_SCALAR);
	level.write(PredType::NATIVE_INT, &level_data);
	
	Attribute timestep = table.createAttribute("timestep", PredType::NATIVE_INT, H5S_SCALAR);
	timestep.write(PredType::NATIVE_INT, &iteration);

	Attribute time = table.createAttribute("time", PredType::NATIVE_DOUBLE, H5S_SCALAR);
	time.write(PredType::NATIVE_DOUBLE, &timeStamp);

	// dx in terms of Cactus: Real seperation from each value
	dvec dx = 1./order * sizeOfPatch;
	Attribute delta = table.createAttribute("delta", PredType::NATIVE_FLOAT, dtuple);
	delta.write(PredType::NATIVE_DOUBLE, dx.data()); // issue: conversion from double to float
	
	const int max_string_length = 60;
	StrType t_str = H5::StrType(H5::PredType::C_S1, max_string_length); // todo: use name.size().
	Attribute aname = table.createAttribute("name", t_str, H5S_SCALAR);
	aname.write(t_str, name.c_str());
  }

}; // class ADERDG2CarpetHDF5Impl

/*************************************************************************************************
 * ADERDG2CarpetHDF5 non-dummy implementation
 *************************************************************************************************/

exahype::plotters::ADERDG2CarpetHDF5::ADERDG2CarpetHDF5(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing):
    Device(postProcessing) {
	bool oneFilePerTimestep = true;
	writer = new exahype::plotters::ADERDG2CarpetHDF5Writer(this, oneFilePerTimestep);
}

// all other methods are stubs
exahype::plotters::ADERDG2CarpetHDF5::~ADERDG2CarpetHDF5() {
	delete writer;
}

void exahype::plotters::ADERDG2CarpetHDF5::init(const std::string& _filename, int _orderPlusOne, int _solverUnknowns, int _writtenUnknowns, const std::string& _select) {
	filename          = _filename;
	order             = _orderPlusOne-1;
	solverUnknowns    = _solverUnknowns;
	select            = _select;
	writtenUnknowns   = _writtenUnknowns;
	iteration         = 0;
	if(DIMENSIONS == 2) {
	  writtenCellIdx       = new kernels::index(order+1, order+1, writtenUnknowns);
	  singleFieldIdx       = new kernels::index(order+1, order+1);
	} else {
	  writtenCellIdx       = new kernels::index(order+1, order+1, order+1, writtenUnknowns);
	  singleFieldIdx       = new kernels::index(order+1, order+1, order+1);
	}
	
	// Determine names of output fields
	writtenQuantitiesNames = new char*[writtenUnknowns];
	std::fill_n(writtenQuantitiesNames, writtenUnknowns, nullptr);
	_postProcessing->writtenQuantitiesNames(writtenQuantitiesNames);
	
	// make sure there are reasonable names everywhere
	for(int u=0; u<writtenUnknowns; u++) {
		char* field_name = writtenQuantitiesNames[u];
		if(!field_name) {
			std::string* replacement_name = new std::string("Q_");
			*replacement_name += toString(u);
			writtenQuantitiesNames[u] = const_cast<char*>(replacement_name->c_str());
		}
	}
	
	// todo at this place: Accept 3D slicing or so, cf. ADERDG2CartesianVTK
	// another nice to have: allow oneFilePerTimestep as a specfile parameter...
	
	writer->init(); // open files and so
}

void exahype::plotters::ADERDG2CarpetHDF5::plotPatch(
        const int cellDescriptionsIndex,
        const int element) {
  auto& aderdgCellDescription = exahype::solvers::ADERDGSolver::getCellDescription(cellDescriptionsIndex,element);

  if (aderdgCellDescription.getType()==exahype::solvers::ADERDGSolver::CellDescription::Type::Cell) {
    double* solverSolution = DataHeap::getInstance().getData(aderdgCellDescription.getSolution()).data();

    plotPatch(
        aderdgCellDescription.getOffset(),
        aderdgCellDescription.getSize(), solverSolution,
        aderdgCellDescription.getCorrectorTimeStamp());
  }
}

void exahype::plotters::ADERDG2CarpetHDF5::plotPatch(
    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
    double* u,
    double timeStamp) {

    // TODO: if we knew that plotting would be serial, we could move *mappedCell to a class property.
    double* mappedCell  = new double[writtenCellIdx->size];

    interpolateCartesianPatch(offsetOfPatch, sizeOfPatch, u, mappedCell, timeStamp);
    writer->plotPatch(offsetOfPatch, sizeOfPatch, mappedCell, timeStamp, iteration, component);
    component++;

    delete[] mappedCell;
}


void exahype::plotters::ADERDG2CarpetHDF5::startPlotting(double _time) {
	time = _time;
	component = 0; // CarpetHDF5 wants the components start with 0.
	_postProcessing->startPlotting(time);
	writer->startPlotting(time, iteration);
}

void exahype::plotters::ADERDG2CarpetHDF5::finishPlotting() {
	_postProcessing->finishPlotting();
	writer->finishPlotting();
	iteration++;
}


void exahype::plotters::ADERDG2CarpetHDF5::interpolateCartesianPatch(
  const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
  const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
  double *u,
  double *mappedCell,
  double timeStamp
) {
  // make sure we only map to ONE written unknown, as this is how CarpetHDF5 works in the moment.
  // assert(writtenUnknowns == 1); // this works for any number of writtenUnknowns.

  double* interpoland = new double[solverUnknowns];
  //double* mappedCell  = new double[writtenCellIdx.size];
  //double* value       = writtenUnknowns==0 ? nullptr : new double[writtenUnknowns];
  
  dfor(i,order+1) {
    for (int unknown=0; unknown < solverUnknowns; unknown++) {
      interpoland[unknown] = 0.0;
      dfor(ii,order+1) { // Gauss-Legendre node indices
        int iGauss = peano::utils::dLinearisedWithoutLookup(ii,order + 1);
        interpoland[unknown] +=
		kernels::equidistantGridProjector1d[order][ii(0)][i(0)] *
		kernels::equidistantGridProjector1d[order][ii(1)][i(1)] *
		#if DIMENSIONS==3
		kernels::equidistantGridProjector1d[order][ii(2)][i(2)] *
		#endif
		u[iGauss * solverUnknowns + unknown];
        assertion3(interpoland[unknown] == interpoland[unknown], offsetOfPatch, sizeOfPatch, iGauss);
      }
    }

    double *value = mappedCell;
    #if DIMENSIONS==3
    value += writtenCellIdx->get(i(2),i(1),i(0),0);
    #else
    value += writtenCellIdx->get(i(1),i(0),0);
    #endif
    assertion(sizeOfPatch(0)==sizeOfPatch(1));
    _postProcessing->mapQuantities(
      offsetOfPatch,
      sizeOfPatch,
      offsetOfPatch + i.convertScalar<double>()* (sizeOfPatch(0)/(order)),
      i,
      interpoland,
      value,
      timeStamp
    );
  }

  delete[] interpoland;
  //if (value!=nullptr)        delete[] value;
}



#endif /* HDF5 */
