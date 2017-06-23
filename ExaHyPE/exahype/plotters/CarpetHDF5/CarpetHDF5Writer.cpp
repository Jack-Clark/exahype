// If HDF5 support is not enabled, there will be no implementation of CarpetHDF5Writer
// in the compiled binary.
#ifdef HDF5

#include "exahype/plotters/CarpetHDF5/CarpetHDF5Writer.h"
#include "peano/utils/Loop.h" // dfor
#include <sstream>
#include <stdexcept>

typedef tarch::la::Vector<DIMENSIONS, double> dvec;
typedef tarch::la::Vector<DIMENSIONS, bool> boolvec;
typedef tarch::la::Vector<DIMENSIONS, int> ivec;

using namespace H5;

// my small C++11 to_string-independent workaround.
template <typename T> std::string toString( T Number ) {
	std::ostringstream ss; ss << Number; return ss.str();
}

exahype::plotters::CarpetHDF5Writer::CarpetHDF5Writer(
	const std::string& _filename,
	int _basisSize,
	int _solverUnknowns,
	int _writtenUnknowns,
	const std::string& _select,
	char** _writtenQuantitiesNames,
	bool _oneFilePerTimestep,
	bool _allUnknownsInOneFile)
	:
	_log("ADERDG2CarpetHDF5Writer"),
	solverUnknowns(_solverUnknowns),
	writtenUnknowns(_writtenUnknowns),
	basisFilename(_filename),
	basisSize(_basisSize),
	select(_select),
	oneFilePerTimestep(_oneFilePerTimestep),
	allUnknownsInOneFile(_allUnknownsInOneFile),
	
	// default values for init(...)-level runtime parameters:
	dim(DIMENSIONS),
	slicer(nullptr),
	component(-100),
	iteration(0),
	writtenQuantitiesNames(_writtenQuantitiesNames),
	
	// hdf5 specific data types
	files(allUnknownsInOneFile ? 1 : writtenUnknowns)
	{

	// todo at this place:  allow _oneFilePerTimestep and _allUnknownsInOneFile to be read off _select.

	// just for convenience/a service, store an indexer for the actual ExaHyPE cells.
	switch(DIMENSIONS) { // simulation dimensions
		case 3:
		patchCellIdx = new kernels::index(basisSize, basisSize, basisSize, writtenUnknowns);
		break;
		
		case 2:
		patchCellIdx = new kernels::index(basisSize, basisSize, writtenUnknowns);
		break;
		
		default:
			throw std::domain_error("CarpetHDF5Writer: I think ExaHyPE only supports 2D and 3D");
	}
	writtenCellIdx = patchCellIdx; // without slicing, this is true.

	slicer = Slicer::bestFromSelectionQuery(select);
	if(slicer) {
		logInfo("init", "Plotting selection "<<slicer->toString()<<" to Files "<<basisFilename);
		if(slicer->getIdentifier() == "CartesianSlicer") {
			dim = static_cast<CartesianSlicer*>(slicer)->targetDim;
		}
	}

	// Important: The CarpetHDF5Writer assumes that dimensional reduction really happens in the
	//            mapped patches. This is not the case in the VTK plotters which don't make a
	//            difference between CartesianSlicer and RegionSlicer.
	switch(dim) { // written dimensions
		case 3:
		writtenCellIdx = new kernels::index(basisSize, basisSize, basisSize, writtenUnknowns);
		singleFieldIdx = new kernels::index(basisSize, basisSize, basisSize);
		break;
		
		case 2:
		writtenCellIdx = new kernels::index(basisSize, basisSize, writtenUnknowns);
		singleFieldIdx = new kernels::index(basisSize, basisSize);
		break;
		
		case 1:
		writtenCellIdx = new kernels::index(basisSize, writtenUnknowns);
		singleFieldIdx = new kernels::index(basisSize);
		break;
		
		default:
			logError("CarpetHDF5Writer", "Error, only dimensions 1, 2, 3 supported. Slicing requested: " << slicer->toString());
			throw std::domain_error("CarpetHDF5Writer does not like your domain"); // har har
	}
	
	// just as shorthands
	patchFieldsSize = patchCellIdx->size;
	writtenFieldsSize = writtenCellIdx->size;
	singleFieldSize = singleFieldIdx->size;
	
	// make sure there are reasonable names everywhere
	for(int u=0; u<writtenUnknowns; u++) {
		if(!writtenQuantitiesNames[u]) {
			std::string* replacement_name = new std::string("Q_");
			*replacement_name += toString(u);
			writtenQuantitiesNames[u] = const_cast<char*>(replacement_name->c_str());
		}
	}
	
	
	// this is the dataspace describing how to write a patch/cell/component
	hsize_t *dims = new hsize_t[dim];
	std::fill_n(dims, dim, basisSize);
	patch_space = DataSpace(dim, dims);
	
	// this is just a vector of rank 1 with dim entries
	const int tupleDim_rank = 1;
	hsize_t tupleDim_len[tupleDim_rank];
	std::fill_n(tupleDim_len, tupleDim_rank, dim);
	dtuple = DataSpace(tupleDim_rank, tupleDim_len);
	
	// open file(s) initially, if neccessary
	if(!oneFilePerTimestep) openH5();
	
	// for Debugging:
	logInfo("CarpetHDF5DebugInfo", "dim=" << dim);
	logInfo("CarpetHDF5DebugInfo", "writtenCellIdx=" << writtenCellIdx->toString());
	logInfo("CarpetHDF5DebugInfo", "singleFieldIdx=" << singleFieldIdx->toString());
}

void exahype::plotters::CarpetHDF5Writer::writeBasicGroup(H5::H5File* file) {
	Group* parameters = new Group(file->createGroup( "/Parameters and Global Attributes" ));
	
	int ranks = 1; // TODO: extend for MPI.
	
	Attribute nioprocs = parameters->createAttribute("nioprocs", PredType::NATIVE_INT, H5S_SCALAR);
	nioprocs.write(PredType::NATIVE_INT, &ranks);
	
	// Todo: add information how much files are printed (oneFilePerTimestep)
	
	// Important todo: List all fields which go into this file.
}

/**
 * Opens or switchs the currently active H5 file or the list of H5 files.
 **/
void exahype::plotters::CarpetHDF5Writer::openH5() {
	std::string local_filename, suffix, prefix, sep("-");
	prefix = basisFilename;
	suffix = (oneFilePerTimestep?(sep + "it" + toString(iteration)):"") + ".h5";
	
	closeH5(); // just to be sure
	int writtenUnknown=0;
	for(auto& file : files) {
		// @TODO: MPI Rank number should go in here.
		local_filename = prefix + (allUnknownsInOneFile ? "" : (sep + writtenQuantitiesNames[writtenUnknown])) + suffix;

		logInfo("open", "Opening File '"<< local_filename << "'");
		file = new H5File(local_filename, H5F_ACC_TRUNC);
		file->setComment("Created by ExaHyPE");
		writeBasicGroup(file);
		writtenUnknown++;
	}
}

void exahype::plotters::CarpetHDF5Writer::closeH5() {
	// flush in any case, even when closing the file. Cf. http://stackoverflow.com/a/31301117
	flushH5();
	for(auto& file : files) {
		if(file) {
			file->close();
			delete file;
			file = nullptr;
		}
	}
}

void exahype::plotters::CarpetHDF5Writer::flushH5() {
	for(auto& file : files) {
		if(file) {
			file->flush(H5F_SCOPE_GLOBAL);
		}
	}
}

void exahype::plotters::CarpetHDF5Writer::startPlotting(double time) {
	component = 0; // CarpetHDF5 wants the components start with 0.

	if(oneFilePerTimestep) openH5();
}
  
void exahype::plotters::CarpetHDF5Writer::finishPlotting() {
	if(oneFilePerTimestep) closeH5();
	else flushH5();

	iteration++;
}

/**
 * This is 2D and 3D, allows several unknowns, named fields and all that.
 **/
void exahype::plotters::CarpetHDF5Writer::plotPatch(
      const dvec& offsetOfPatch, const dvec& sizeOfPatch, const dvec& dx,
      double* mappedCell, double timeStamp) {
	for(int writtenUnknown=0; writtenUnknown < writtenUnknowns; writtenUnknown++) {
		H5::H5File* target = files[allUnknownsInOneFile ? 0 : writtenUnknown];
		plotPatchForSingleUnknown(offsetOfPatch, sizeOfPatch, dx, mappedCell, timeStamp, writtenUnknown, target);
	} // for writtenUnknown
	component++;
}

void exahype::plotters::CarpetHDF5Writer::plotPatchForSingleUnknown(
      const dvec& offsetOfPatch, const dvec& sizeOfPatch, const dvec& dx,
      double* mappedCell, double timeStamp,
      int writtenUnknown, H5::H5File* target) {
	assertion(target != nullptr);
	
	// in CarpetHDF5, the field name *must* contain a "::"
	std::string name("ExaHyPE::");
	char* field_name = writtenQuantitiesNames[writtenUnknown];
	name += field_name ? field_name : "miserable-failure";
	
	std::stringstream component_name;
	component_name << name << " it=" << iteration << "tl=0 m=0 rl=0 c=" << component;
	
	// 1) Compose a continous storage which is suitable.
	// TODO: I'm sure HDF5 provides a more performant way to interpret the different data layout.
	double *componentPatch = new double[singleFieldIdx->size];
	dfor(i,basisSize) {
		switch(dim) {
			case 3:
			componentPatch[singleFieldIdx->get(i(2),i(1),i(0))] = mappedCell[writtenCellIdx->get(i(2),i(1),i(0),writtenUnknown)];
			break;

			case 2:
			componentPatch[singleFieldIdx->get(i(1),i(0))] = mappedCell[writtenCellIdx->get(i(1),i(0),writtenUnknown)];
			break;
			
			case 1:
			componentPatch[singleFieldIdx->get(i(0))] = mappedCell[writtenCellIdx->get(i(0),writtenUnknown)];
			break;
		}
	}
	
	DataSet table = target->createDataSet(component_name.str(), PredType::NATIVE_FLOAT, patch_space);
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
	Attribute delta = table.createAttribute("delta", PredType::NATIVE_FLOAT, dtuple);
	delta.write(PredType::NATIVE_DOUBLE, dx.data()); // issue: conversion from double to float
	
	StrType t_str = H5::StrType(H5::PredType::C_S1, name.size()+1); // Todo: not sure about +1 for \0
	Attribute aname = table.createAttribute("name", t_str, H5S_SCALAR);
	aname.write(t_str, name.c_str());
}


#endif /* HDF5 */
