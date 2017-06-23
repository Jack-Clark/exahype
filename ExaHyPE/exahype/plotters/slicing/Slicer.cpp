// Implementing all of the slicers in a single file to decrease
// compilation time, for the moment.

#include "Slicer.h"
#include "RegionSlicer.h"
#include "CartesianSlicer.h"

#include "tarch/logging/Log.h"
#include "exahype/Parser.h"

#include <sstream>

using namespace exahype::plotters;
using namespace std;

// storage for the RegionSlicer limits
const double RegionSlicer::defaultLeftBottomFront = -std::numeric_limits<double>::max();
const double RegionSlicer::defaultRightTopBack    = +std::numeric_limits<double>::max();

static tarch::logging::Log _log("exahype::plotters::Slicer");

CartesianSlicer::CartesianSlicer(const dvec& _req, const ivec& _active, int _baseDim) : 
		targetDim(_baseDim - tarch::la::sum(_active)),
		baseDim(_baseDim),
		req(_req),
		active(_active),
		activeAxes(-1),
		runningAxes(-1) {
		
	for(int i=0; i<DIMENSIONS; i++) {
		activeAxes(i) = disabled;
		runningAxes(i) = disabled;
		
		// This algorithm is crazy. Needed a lot of debugging with standalone
		// examples, but now its tested for DIM<=3.
		
		for(int j=i; j<DIMENSIONS; j++) { // forward check for actives
			if(active(j)) {
				activeAxes(i)=j;
				for(int k=0; k<i; k++) { // backward check if not already included
					if(activeAxes(k)==j)
						activeAxes(i)=disabled;
				}
				if(activeAxes(i)!=disabled)
					break;
			}
		}
		
		for(int j=i; j<DIMENSIONS; j++) { // forward check for actives
			if(!active(j)) {
				runningAxes(i)=j;
				for(int k=0; k<i; k++) { // backward check if not already included
					if(runningAxes(k)==j)
						runningAxes(i)=disabled;
				}
				if(runningAxes(i)!=disabled)
					break;
			}
		}
	}
}
	
CartesianSlicer* CartesianSlicer::fromSelectionQuery(const std::string& select) {
	dvec r;
	r(0) = Parser::getValueFromPropertyString(select, "x");
	r(1) = Parser::getValueFromPropertyString(select, "y");
	#if DIMENSIONS==3
	r(2) = Parser::getValueFromPropertyString(select, "z");
	#endif
	ivec ron;
	// NaN means the property was not present in the select string
	for(int i=0; i<DIMENSIONS; i++) { ron(i) = (r(i)!=r(i)) ? 0 : 1; }
	
	return new CartesianSlicer(r, ron);
}

/**
 * A variant of tarch::la::Vector::toString which replcaes infVal with repl.
 **/
std::string valueReplPrinter(const tarch::la::Vector<DIMENSIONS, double>& vec, const double infVal, const std::string& repl) {
	stringstream s;
	s << "[";
	for(int i=0; i < DIMENSIONS; i++) {
		if(vec[i] == infVal) s << repl;
		else s << vec[i];
		if(i + 1 < DIMENSIONS) s << ",";
	}
	s << "]";
	return s.str();
}

std::string RegionSlicer::toString() const {
	// a beautiful infinity character in utf8. Could also just use "inf".
	// Unicode works in most Linux and Mac terminals but not Windows. (https://stackoverflow.com/a/12020179)
	std::string inf = "\u221E", plus = "+", minus = "-";
	
	stringstream s;
	s << "RegionSlicer("
	  << valueReplPrinter(_regionOfInterestLeftBottomFront, defaultLeftBottomFront, minus + inf)
	  << ","
	  << valueReplPrinter(_regionOfInterestRightTopBack, defaultRightTopBack, plus + inf)
	  << ")";
	
	return s.str();
}

std::string CartesianSlicer::toString() const {
	stringstream s;
	s << "CartesianSlicer(Dim["<<baseDim<<"] to Dim["<<targetDim<<"], req="<<req<<")";
	return s.str();
}

// debugging stuff, should not be operator<< but be named like "debugString" or so.
//std::ostream& operator<<(std::ostream &s,const CartesianSlicer& c) {
std::string CartesianSlicer::debugVerbose() {
	stringstream s;
	s << "CartesianSlicer, Reducing Dimension " << baseDim << " to " << targetDim << ":\n";
	s << "   req = " << req << "\n";
	s << "   active = " << active << "\n";
	s << "   activeAxes = " << activeAxes << "\n";
	s << "   runningAxes = " << runningAxes << "\n";
	return s.str();
}


RegionSlicer* RegionSlicer::fromSelectionQuery(const std::string& select) {
	dvec regionOfInterestLeftBottomFront, regionOfInterestRightTopBack;
	double x;
	x = Parser::getValueFromPropertyString( select, "left" );
	regionOfInterestLeftBottomFront(0) = x!=x ? defaultLeftBottomFront : x; // "-", min
	x = Parser::getValueFromPropertyString( select, "bottom" );
	regionOfInterestLeftBottomFront(1) = x!=x ? defaultLeftBottomFront : x; // "-", min
	#if DIMENSIONS==3
	x = Parser::getValueFromPropertyString( select, "front" );
	regionOfInterestLeftBottomFront(2) = x!=x ? defaultLeftBottomFront : x; // "-", min
	#endif
	
	x = Parser::getValueFromPropertyString( select, "right" );
	regionOfInterestRightTopBack(0) = x!=x ? defaultRightTopBack : x;
	x = Parser::getValueFromPropertyString( select, "top" );
	regionOfInterestRightTopBack(1) = x!=x ? defaultRightTopBack : x;
	#if DIMENSIONS==3
	x = Parser::getValueFromPropertyString( select, "back" );
	regionOfInterestRightTopBack(2) = x!=x ? defaultRightTopBack : x;
	#endif
	
	return new RegionSlicer(regionOfInterestLeftBottomFront, regionOfInterestRightTopBack);
}

Slicer* Slicer::bestFromSelectionQuery(const std::string& select) {
	logInfo("bestFromSelectionQuery", "Scanning plotting selection query '"<<select<<"'");
	
	// Build up the registry:
	Slicer *a = CartesianSlicer::fromSelectionQuery(select);
	Slicer *b = RegionSlicer::fromSelectionQuery(select);

	if(a->clips() && b->clips()) {
		logError("bestFromSelectionQuery", "Warning: Several slicing strategies apply to the given arguments '"<<select<<"'. I choose " << a->getIdentifier());
	}

	if(a->clips()) { delete b; return a;}
	if(b->clips()) { delete a; return b; }
	
	// nothing clips
	delete a; delete b;
	return new NonSlicer;
}

	
