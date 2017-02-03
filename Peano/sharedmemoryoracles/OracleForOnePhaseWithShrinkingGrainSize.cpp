#include "sharedmemoryoracles/OracleForOnePhaseWithShrinkingGrainSize.h"
#include "peano/utils/Globals.h"
#include "tarch/Assertions.h"
#include "tarch/multicore/Core.h"


#include <cstdlib>
#include <limits>
#include <fstream>
#include <stdexcept>


tarch::logging::Log  sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::_log( "sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize" );

const double   sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::_InitialRelativeAccuracy(1e-2);
const double   sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::_TimingMax( 65536.0 );



sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::OracleForOnePhaseWithShrinkingGrainSize(bool learn):
  _activeMethodTrace(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling),
  _learn(learn),
  _measurements() {
}


bool sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry::operator<(const DatabaseEntry& cmp) const {
  return ( _biggestProblemSize<cmp._biggestProblemSize );
}


sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry&  sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::getDatabaseEntry(
  int problemSize,
  peano::datatraversal::autotuning::MethodTrace askingMethod
) {
  if ( _measurements.count(askingMethod)==0 ) {
    _measurements.insert( std::pair<peano::datatraversal::autotuning::MethodTrace,MethodTraceData >(askingMethod,MethodTraceData()) );
    _measurements[askingMethod].push_back( DatabaseEntry(2) );
    assertion( _measurements.count(askingMethod)==1 );
    logInfo(
      "getDatabaseEntry(int)",
      "inserted trivial entry for " + peano::datatraversal::autotuning::toString(askingMethod)
      << ": " << _measurements[askingMethod].rbegin()->toString()
    );
  }

  while (_measurements[askingMethod].rbegin()->_biggestProblemSize<problemSize) {
    _measurements[askingMethod].push_back( DatabaseEntry(
      *_measurements[askingMethod].rbegin(),
      _measurements[askingMethod].rbegin()->_biggestProblemSize*2
    ));
    logDebug(
      "getDatabaseEntry(int)",
      "inserted new entry for " + peano::datatraversal::autotuning::toString(askingMethod)
      << ": " << _measurements[askingMethod].rbegin()->toString()
    );
  }

  MethodTraceData::iterator entry = _measurements[askingMethod].begin();
  while (entry->_biggestProblemSize<problemSize) {
    entry++;
  }

  DatabaseEntry& result = *entry;
  return result;
}


peano::datatraversal::autotuning::GrainSize  sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::parallelise(int problemSize, peano::datatraversal::autotuning::MethodTrace askingMethod) {
  assertion( askingMethod != peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling );

  assertion(askingMethod!=peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling);
  assertion( _measurements.count(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling)==0 );

  auto databaseEntry = getDatabaseEntry(problemSize,askingMethod);

  assertion( _measurements.count(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling)==0 );
  assertion( _measurements.count(askingMethod)>0 );

  const bool trackTime         = (_activeMethodTrace==askingMethod) && (databaseEntry._searchDelta>0);

  logDebug(
    "parallelise()",
    "will track time for " << toString(askingMethod) << "=" << trackTime <<
    " with active trace " << toString(_activeMethodTrace)
  );

  const int chosenGrainSize = databaseEntry._currentGrainSize<problemSize ? databaseEntry._currentGrainSize : 0;
  return peano::datatraversal::autotuning::GrainSize(
    chosenGrainSize,
    trackTime,
    problemSize,
    askingMethod, this
  );
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::widenAccuracyOfCurrentlyStudiedMethodTraceAndRandomlyRestart() {
  bool widenedEntry = false;
  for (auto& p: _measurements[_activeMethodTrace]) {
    const bool widenThisEntry = p._searchDelta>0 && p._currentMeasurement.getAccuracy()>0.0;
    if (widenThisEntry) {
      p._currentMeasurement.increaseAccuracy(0.9);
    }
    widenedEntry |= widenThisEntry;
  }
  if (!widenedEntry && rand()%100==0) {
    logInfo( "widenAccuracyOfCurrentlyStudiedMethodTraceAndRandomlyRestart(...)", "restart all measurments of " << toString(_activeMethodTrace) );
    for (auto& p: _measurements[_activeMethodTrace]) {
      p.restart();
    }
  }
}


bool sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::isStableDatabaseEntrySet( const peano::datatraversal::autotuning::MethodTrace&  askingMethod) const {
  if ( _measurements.count(askingMethod)==0 ) {
    return true;
  }
  else {
    bool result             = true;
    bool noValidAccuracyYet = true;

    for (auto p: _measurements.at(askingMethod)) {
      result             &= ( (p._searchDelta==0) || (p._currentMeasurement.getAccuracy()==0.0) );
      noValidAccuracyYet &= (p._currentMeasurement.getAccuracy()==0);
    }

    return result && !noValidAccuracyYet;
  }
}



void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::changeMeasuredMethodTrace() {
  assertion( _measurements.count(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling)==0 );
  assertion( !_measurements.empty() );

  const bool randomiseSelection   = true;

  // We just make it big enough to run through every variant twice if we use a deterministic scheme.
  int remainingTriesToFindSearchingTrace = (int)(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling);
  if (randomiseSelection) {
    _activeMethodTrace             = peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling;

    while ( _measurements.count(_activeMethodTrace)==0 ) {
      _activeMethodTrace = peano::datatraversal::autotuning::toMethodTrace( rand() % (int)(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling) );
      if (
        remainingTriesToFindSearchingTrace>0
        &&
        _measurements.count(_activeMethodTrace)==1
        &&
        isStableDatabaseEntrySet(_activeMethodTrace)
      ) {
        remainingTriesToFindSearchingTrace--;
        logDebug( "changeMeasuredMethodTrace()", "skip " << toString(_activeMethodTrace) );
        _activeMethodTrace = peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling;
      }
    }
  }
  else {
    do {
      _activeMethodTrace = peano::datatraversal::autotuning::toMethodTrace( static_cast<int>(_activeMethodTrace) + 1 );
      if (_activeMethodTrace>=peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling) {
        _activeMethodTrace = peano::datatraversal::autotuning::toMethodTrace( 0 );
      }
      remainingTriesToFindSearchingTrace--;
    }
    while (
      _measurements.count(_activeMethodTrace)==0
      ||
      ( remainingTriesToFindSearchingTrace>0 && isStableDatabaseEntrySet(_activeMethodTrace))
    );
  }

  assertion(_measurements.count(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling)==0);
  assertion(_activeMethodTrace!=peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling);

  assertion(_activeMethodTrace!=peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling);
  assertion(_measurements.count(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling)==0);

  logDebug( "changeMeasuredMethodTrace()", "next active method trace " << toString(_activeMethodTrace) );
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry::learn() {
  assertion( _currentMeasurement.isAccurateValue() );
  assertion( _searchDelta>0 );

  if ( _currentMeasurement.getValue() < _previousMeasuredTime ) {
    logInfo( "learn()", "found better scaling parameter choice/serial runtime for: " << toString() );

    while ( _currentGrainSize - _searchDelta <= 0 && _searchDelta>0 ) {
      _searchDelta /= 2;
    }

    _currentGrainSize     -= _searchDelta;
    _previousMeasuredTime  = _currentMeasurement.getValue();
    _currentMeasurement.erase();
    _currentMeasurement.increaseAccuracy(2.0);

    logInfo( "learn()", "continue with " << toString() );
  }
  else {
    logInfo( "learn()", "parameter choice for " << toString() << " does not scale" );

    _currentGrainSize     += _searchDelta;
    _previousMeasuredTime  = _TimingMax;
    _currentMeasurement.erase();
    _currentMeasurement.increaseAccuracy(2.0);

    if ( _biggestProblemSize<=tarch::multicore::Core::getInstance().getNumberOfThreads() ) {
      _searchDelta--;
    }
    else if (
      _searchDelta > tarch::multicore::Core::getInstance().getNumberOfThreads()
    ) {
      _searchDelta /= std::max(2,tarch::multicore::Core::getInstance().getNumberOfThreads());
    }
    else {
      _searchDelta /= 2;
    }

    logInfo( "learn()", "continue with " << toString() );
  }

  assertion( _currentGrainSize>0 );
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::parallelSectionHasTerminated(int problemSize, peano::datatraversal::autotuning::MethodTrace askingMethod, double costPerProblemElement) {
  assertion( askingMethod!=peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling );

  auto& databaseEntry = getDatabaseEntry(problemSize,askingMethod);
  if (databaseEntry._currentMeasurement.getAccuracy()==0.0 ) {
    const double computeTime   = costPerProblemElement * static_cast<double>(problemSize);
    databaseEntry._currentMeasurement.setAccuracy( computeTime * _InitialRelativeAccuracy );

    logDebug(
      "parallelSectionHasTerminated(...)",
      "fix accuracy for " << toString(askingMethod) << " to " << databaseEntry.toString()
    );
  }

  databaseEntry._currentMeasurement.setValue( costPerProblemElement );
}


std::string sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry::toString() const {
  std::ostringstream msg;
  msg <<        _biggestProblemSize
      << "," << _currentGrainSize
      << "," << _previousMeasuredTime
      << "," << _searchDelta
      << "," << _currentMeasurement.getAccuracy()
      << "," << _currentMeasurement.toString()
      << ")";
  return msg.str();
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::plotStatistics(std::ostream& out, int oracleNumber) const {
  out << "# " << std::endl;
  out << "# trace=biggest problem size, current grain size, previous measured time, search delta, accuracy, current measurement" << std::endl;
  out << "# -------------------------------------------------------------------------------------------------------------------" << std::endl;
  out << "# " << std::endl;
  out << "begin OracleForOnePhaseWithShrinkingGrainSize" << std::endl;
  out << "initial-relative-accuracy=" << _InitialRelativeAccuracy << std::endl;
  out << "adapter-number=" << oracleNumber << std::endl;

  for (auto measurement: _measurements)
  for (auto p: measurement.second) {
    out << peano::datatraversal::autotuning::toString(measurement.first)
        << "=" << p.toString() << std::endl;
  }

  out << "end OracleForOnePhaseWithShrinkingGrainSize" << std::endl;

  logDebug( "plotStatistics(std::ostream,int)", "piped statistics for oracle no " << oracleNumber );
}


sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry::DatabaseEntry(int problemSize) {
  _biggestProblemSize   = problemSize;
  _currentGrainSize     = problemSize;
  _currentMeasurement   = tarch::timing::Measurement( 0.0 );
  _previousMeasuredTime = _TimingMax;
  _searchDelta          = problemSize < tarch::multicore::Core::getInstance().getNumberOfThreads()*2 ? problemSize/2 : problemSize - problemSize / tarch::multicore::Core::getInstance().getNumberOfThreads();
}


sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry::DatabaseEntry( DatabaseEntry& prototype, int newProblemSize ):
  DatabaseEntry(newProblemSize) {
  assertion(prototype._biggestProblemSize<newProblemSize);
  if (prototype._currentGrainSize < prototype._biggestProblemSize) {
    _currentGrainSize = prototype._currentGrainSize;
    restart();
  }
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry::restart() {
  int newCurrentGrainSize = (_biggestProblemSize + _currentGrainSize)/2;
  _currentGrainSize     = newCurrentGrainSize;
  _currentMeasurement   = tarch::timing::Measurement( 0.0 );
  _previousMeasuredTime = _TimingMax;
  _searchDelta          = newCurrentGrainSize - _currentGrainSize;

  if (_searchDelta==0) {
    _searchDelta = _biggestProblemSize/2;
  }
}





sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::DatabaseEntry::DatabaseEntry(std::string  rightString) {
  std::string leftToken = "";
  std::string methodTrace;

  leftToken   = rightString.substr( 0, rightString.find(",") );
  rightString = rightString.substr( leftToken.size()+1 );
  _biggestProblemSize = std::stoi(leftToken);
  logDebug( "loadStatistics(...)", "got biggest problem size " << _biggestProblemSize );

  leftToken   = rightString.substr( 0, rightString.find(",") );
  rightString = rightString.substr( leftToken.size()+1 );
  _currentGrainSize = std::stoi(leftToken);
  logDebug( "loadStatistics(...)", "got current grain size " <<  _currentGrainSize );

  leftToken   = rightString.substr( 0, rightString.find(",") );
  rightString = rightString.substr( leftToken.size()+1 );
  _previousMeasuredTime = std::stof(leftToken);
  logDebug( "loadStatistics(...)", "previous measured time is " <<  _previousMeasuredTime );

  leftToken   = rightString.substr( 0, rightString.find(",") );
  rightString = rightString.substr( leftToken.size()+1 );
  _searchDelta = std::stoi(leftToken);
  logDebug( "loadStatistics(...)", "search delta is " <<  _searchDelta );

  leftToken   = rightString.substr( 0, rightString.find(",") );
  rightString = rightString.substr( leftToken.size()+1 );
  double accuracy = std::stof(leftToken);
  logDebug( "loadStatistics(...)", "accuracy is " <<  accuracy << ". Ignore remainder of this line");

  _currentMeasurement.erase();
  _currentMeasurement.setAccuracy( accuracy );
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::loadStatistics(const std::string& filename, int oracleNumber) {
  std::ifstream file(filename);
  std::string str = "";

  bool        tagOpen = false;
  while (std::getline(file, str)) {
    tagOpen &= str.compare("end OracleForOnePhaseWithShrinkingGrainSize")!=0;

    if (tagOpen) {
      #ifdef __EXCEPTIONS
      try {
      #endif

      std::string leftToken = "";
      std::string rightString = str;

      leftToken   = rightString.substr( 0, rightString.find("=") );
      rightString = rightString.substr( leftToken.size()+1 );
      peano::datatraversal::autotuning::MethodTrace  methodTrace = peano::datatraversal::autotuning::toMethodTrace(leftToken);
      logDebug( "loadStatistics(...)", "parse properties for " << toString(methodTrace) );

      DatabaseEntry newEntry(rightString);

      _measurements[methodTrace].push_back( newEntry );

      logDebug( "loadStatistics(...)", "added " << newEntry.toString() << " for " << toString(methodTrace) );
      #ifdef __EXCEPTIONS
      }
      catch (std::out_of_range& exception) {
        logWarning( "loadStatistics(...)", "failed to parse shared memory configuration file " << filename << " with error " << exception.what() << " in adapter " << oracleNumber);
      }
      #endif
    }

    // Older GCC versions require an explicit cast here
    tagOpen |= str.compare( "adapter-number=" + std::to_string( (long long)oracleNumber) )==0;
  }

  assertion( _measurements.count(peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling)==0 );
}


sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::~OracleForOnePhaseWithShrinkingGrainSize() {
}


peano::datatraversal::autotuning::OracleForOnePhase* sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::createNewOracle() const {
  return new OracleForOnePhaseWithShrinkingGrainSize(_learn);
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::deactivateOracle() {
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::clearAllMeasurementsBesidesActiveOne() {
  for (auto& measurement: _measurements) {
    if (measurement.first!=_activeMethodTrace) {
      for (auto& p: measurement.second) {
        p._currentMeasurement.erase();
      }
    }
  }
}


void sharedmemoryoracles::OracleForOnePhaseWithShrinkingGrainSize::activateOracle() {
  if (_learn) {
    //
    // First check is very important. If we skip it, the second statement would
    // insert elements into map
    //
    if (
      _activeMethodTrace!=peano::datatraversal::autotuning::MethodTrace::NumberOfDifferentMethodsCalling
    ) {
      for (auto& p: _measurements[_activeMethodTrace]) {
	      bool oneMeasurementDidLearn = false;
        if (
          p._currentMeasurement.isAccurateValue()
          &&
          p._searchDelta>0
        ) {
          logInfo( "activateOracle()", "found entry that should learn for trace " << toString(_activeMethodTrace) );
          p.learn();
          oneMeasurementDidLearn = true;
        }

        if (oneMeasurementDidLearn) {
          clearAllMeasurementsBesidesActiveOne();
        }
      }
    }

    if (!_measurements.empty() ) {
      changeMeasuredMethodTrace();
      widenAccuracyOfCurrentlyStudiedMethodTraceAndRandomlyRestart();
    }
  }
}
