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

#include "tarch/logging/Log.h"
#include "tarch/tests/TestCaseRegistry.h"
#include "tarch/logging/CommandLineLogger.h"
#include "tarch/logging/LogFilterFileReader.h"
#include "tarch/parallel/Node.h"

#include "peano/peano.h"

#include "exahype/main.h"
#include "exahype/Parser.h"
#include "exahype/Vertex.h"
#include "exahype/runners/Runner.h"
#include "buildinfo.h" // this file is expected in the user application directory

#include "kernels/KernelCalls.h"

#include "kernels/GaussLegendreQuadrature.h"
#include "kernels/DGMatrices.h"

#include <vector>
#include <cstdlib> // getenv, exit
#include <iostream>
#include <cstdio>

tarch::logging::Log _log("");

/**
 * The ping pong test has to be triggered by main very very early. There should
 * be no other message in the MPI subsystem.
 */
void exahype::pingPoingTest() {
  #if defined(Asserts) && defined(Parallel)
  logInfo( "run()", "start ping-pong test" );
  exahype::Vertex::initDatatype();
  exahype::Vertex sendVertex[5];

  if (tarch::parallel::Node::getInstance().getNumberOfNodes()>1) {
    if (tarch::parallel::Node::getInstance().getRank()==0) {
      sendVertex[0].setPosition( tarch::la::Vector<DIMENSIONS,double>(2.0), 4);
      sendVertex[0].setAdjacentRank( 0, 10 );
      sendVertex[0].setAdjacentRank( 1, 11 );
      sendVertex[0].setAdjacentRank( 2, 12 );
      sendVertex[0].setAdjacentRank( 3, 13 );
      sendVertex[1].setPosition( tarch::la::Vector<DIMENSIONS,double>(3.0), 5);
      sendVertex[1].setAdjacentRank( 0, 20 );
      sendVertex[1].setAdjacentRank( 1, 21 );
      sendVertex[1].setAdjacentRank( 2, 22 );
      sendVertex[1].setAdjacentRank( 3, 23 );
      sendVertex[2].setPosition( tarch::la::Vector<DIMENSIONS,double>(4.0), 6);
      sendVertex[2].setAdjacentRank( 0, 30 );
      sendVertex[2].setAdjacentRank( 1, 31 );
      sendVertex[2].setAdjacentRank( 2, 32 );
      sendVertex[2].setAdjacentRank( 3, 33 );

      logInfo( "run()", "send one vertex" );
      sendVertex[0].send(1,100,false,-1);
      logInfo( "run()", "vertex left system" );

      logInfo( "run()", "send three vertices from call stack" );
      MPI_Send( sendVertex, 3, exahype::Vertex::MPIDatatypeContainer::Datatype, 1, 1, tarch::parallel::Node::getInstance().getCommunicator() );
      logInfo( "run()", "vertices left system" );
    }
    if (tarch::parallel::Node::getInstance().getRank()==1) {
      logInfo( "run()", "start to receive single vertex " );
      exahype::Vertex receivedVertex;
      receivedVertex.receive(0,100,false,-1);
      logInfo( "run()", "received vertex " << receivedVertex.toString() );
      assertion1( receivedVertex.getLevel()==4, receivedVertex.toString() );
      assertion1( receivedVertex.getX()(0)==2.0, receivedVertex.toString() );
      assertion1( receivedVertex.getX()(1)==2.0, receivedVertex.toString() );
      #ifdef Dim3
      assertion1( receivedVertex.getX()(2)==2.0, receivedVertex.toString() );
      #endif

      exahype::Vertex receivedVertices[5];
      logInfo( "run()", "start to receive three vertices on call stack" );
      MPI_Recv( receivedVertices, 3, exahype::Vertex::MPIDatatypeContainer::Datatype, 0, 1, tarch::parallel::Node::getInstance().getCommunicator(), MPI_STATUS_IGNORE );
      logInfo( "run()", "received vertices" );
      assertion3( receivedVertices[0].getLevel()==4,  receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      assertion3( receivedVertices[0].getX()(0)==2.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      assertion3( receivedVertices[0].getX()(1)==2.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      #ifdef Dim3
      assertion3( receivedVertices[0].getX()(2)==2.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      #endif

      assertion3( receivedVertices[1].getLevel()==5,  receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      assertion3( receivedVertices[1].getX()(0)==3.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      assertion3( receivedVertices[1].getX()(1)==3.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      #ifdef Dim3
      assertion3( receivedVertices[1].getX()(2)==3.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      #endif

      assertion3( receivedVertices[2].getLevel()==6,  receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      assertion3( receivedVertices[2].getX()(0)==4.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      assertion3( receivedVertices[2].getX()(1)==4.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      #ifdef Dim3
      assertion3( receivedVertices[2].getX()(2)==4.0, receivedVertices[0].toString(), receivedVertices[1].toString(), receivedVertices[2].toString() );
      #endif
      logInfo( "run()", "first part of ping pong test ok" );
    }
    MPI_Barrier( tarch::parallel::Node::getInstance().getCommunicator() );

    if (tarch::parallel::Node::getInstance().getRank()==0) {
      //exahype::Vertex* heapVertices = new exahype::Vertex[5];
      exahype::Vertex heapVertices[5];
      logInfo( "run()", "wait for three vertices to arrive on heap" );
      MPI_Recv( heapVertices, 3, exahype::Vertex::MPIDatatypeContainer::Datatype, 1, 1, tarch::parallel::Node::getInstance().getCommunicator(), MPI_STATUS_IGNORE );
      logInfo( "run()", "vertices have arrived" );

      assertion3( heapVertices[0].getLevel()==4,  heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      assertion3( heapVertices[0].getX()(0)==2.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      assertion3( heapVertices[0].getX()(1)==2.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      #ifdef Dim3
      assertion3( heapVertices[0].getX()(2)==2.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      #endif

      assertion3( heapVertices[1].getLevel()==5,  heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      assertion3( heapVertices[1].getX()(0)==3.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      assertion3( heapVertices[1].getX()(1)==3.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      #ifdef Dim3
      assertion3( heapVertices[1].getX()(2)==3.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      #endif

      assertion3( heapVertices[2].getLevel()==6,  heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      assertion3( heapVertices[2].getX()(0)==4.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      assertion3( heapVertices[2].getX()(1)==4.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      #ifdef Dim3
      assertion3( heapVertices[2].getX()(2)==4.0, heapVertices[0].toString(), heapVertices[1].toString(), heapVertices[2].toString() );
      #endif

      //delete[] heapVertices;
      logInfo( "run()", "second part of ping-poing test ok" );
    }
    if (tarch::parallel::Node::getInstance().getRank()==1) {
      exahype::Vertex* heapVertices = new exahype::Vertex[5];
      heapVertices[0].setPosition( tarch::la::Vector<DIMENSIONS,double>(2.0), 4);
      heapVertices[1].setPosition( tarch::la::Vector<DIMENSIONS,double>(3.0), 5);
      heapVertices[2].setPosition( tarch::la::Vector<DIMENSIONS,double>(4.0), 6);
      logInfo( "run()", "send out three vertices from heap" );
      MPI_Send( heapVertices, 3, exahype::Vertex::MPIDatatypeContainer::Datatype, 0, 1, tarch::parallel::Node::getInstance().getCommunicator() );
      delete[] heapVertices;
      logInfo( "run()", "vertices have left the system" );
    }
    MPI_Barrier( tarch::parallel::Node::getInstance().getCommunicator() );
  }
  #endif
}


int exahype::main(int argc, char** argv) {
  peano::fillLookupTables();

  //
  //   Setup environment
  // =====================
  //
  int parallelSetup = peano::initParallelEnvironment(&argc, &argv);
  if (parallelSetup != 0) {
#ifdef Parallel
    // Please do not use the logging if MPI doesn't work properly.
    std::cerr << "mpi initialisation wasn't successful. Application shut down"
              << std::endl;
#else
    _log.error("main()",
               "mpi initialisation wasn't successful. Application shut down");
#endif
    return parallelSetup;
  }

  int sharedMemorySetup = peano::initSharedMemoryEnvironment();
  if (sharedMemorySetup != 0) {
    logError("main()",
             "shared memory initialisation wasn't successful. Application shut "
             "down");
    return sharedMemorySetup;
  }

//  pingPoingTest();

  //
  //   Parse config file
  // =====================
  //
  std::string progname = argv[0];

  if (argc < 2) {
    logError("main()", "Usage: " << progname << " config-file [additional args passed to Solver...]");
    return -1;
  }

  // cmdlineargs contains all argv expect the progname.
  std::vector<std::string> cmdlineargs(argv + 1, argv + argc);
  std::string firstarg = cmdlineargs[0];

  bool showHelp    = firstarg == "-h" || firstarg == "--help";
  bool showVersion = firstarg == "-v" || firstarg == "--version";

  if(showHelp) {
    help(progname);
    return EXIT_SUCCESS;
  }

  if(showVersion) {
    version(progname);
    return EXIT_SUCCESS;
  }

  exahype::Parser parser;
  parser.readFile(firstarg);

  if (!parser.isValid()) {
    logError("main()", "invalid config file. Quit");
    return -2;
  }

  //
  //   Init solver registries
  // =====================================
  //
  kernels::initSolvers(parser, cmdlineargs);

  /*
  // We had this before to show the real solver registration, ie. like
  //    exahype::solvers::RegisteredSolvers->toString(ostream);
  // This needs kernels::initSolvers(parser...) to be called.
  if (onlyShowVersion) {
    version();
    kernels::finalise();
    return 0;
  }
  */

  //
  //   Configure the logging
  // =========================
  //
  tarch::logging::CommandLineLogger::getInstance().clearFilterList();
  #ifdef Parallel
  tarch::logging::CommandLineLogger::getInstance().setLogFormat(
      " ",    // columnSeparator
      true,   // logTimeStamp
      false,  // logTimeStampHumanReadable
      true,   // logMachineName
      true,   // logMessageType
      true,   // logTrace
      parser.getLogFileName() );
  #elif defined(Asserts) || defined(Debug)
  tarch::logging::CommandLineLogger::getInstance().setLogFormat(
      " ",    // columnSeparator
      true,   // logTimeStamp
      false,  // logTimeStampHumanReadable
      false,  // logMachineName
      true,   // logMessageType
      true,   // logTrace
      parser.getLogFileName() );
  #else
  tarch::logging::CommandLineLogger::getInstance().setLogFormat(
      " ",    // columnSeparator
      true,   // logTimeStamp
      false,  // logTimeStampHumanReadable
      false,  // logMachineName
      true,   // logMessageType
      false,   // logTrace
      parser.getLogFileName() );
  #endif

  tarch::logging::CommandLineLogger::getInstance().clearFilterList();
  if (!tarch::logging::LogFilterFileReader::parsePlainTextFile(
          "exahype.log-filter")) {
    tarch::logging::CommandLineLogger::getInstance().clearFilterList();
    tarch::logging::CommandLineLogger::getInstance().addFilterListEntry(
        ::tarch::logging::CommandLineLogger::FilterListEntry("info", false));
    #if !defined(Asserts)
    tarch::logging::CommandLineLogger::getInstance().addFilterListEntry(
        ::tarch::logging::CommandLineLogger::FilterListEntry(
            "info", -1, "peano::grid", true));
    #endif
    tarch::logging::CommandLineLogger::getInstance().addFilterListEntry(
        ::tarch::logging::CommandLineLogger::FilterListEntry("debug", true));
    tarch::logging::CommandLineLogger::getInstance().addFilterListEntry(
        ::tarch::logging::CommandLineLogger::FilterListEntry("debug", -1,
                                                             "exahype", false));
  }

//
//   Run tests
// =============
// Our unit tests do cover the generic ADER-DG kernels. The generic kernels do
// parallelise. As a consequence, they connect to the autotuning feature.
// Autotuning however is not set up yet, so this will fail. We therefore
// disable the unit tests in shared memory mode.
//
#if (defined(Debug) || defined(Asserts)) && !defined(SharedMemoryParallelisation)
if(! std::getenv("EXAHYPE_SKIP_TESTS")) { // cf issue #74
  tarch::tests::TestCaseRegistry::getInstance().getTestCaseCollection().run();
  int testExitCode = tarch::tests::TestCaseRegistry::getInstance()
                         .getTestCaseCollection()
                         .getNumberOfErrors();

  if (testExitCode != 0) {
    logError("main()", "unit tests failed. Quit.");
    return -2;
  }
} else {
  logInfo("main()", "Skipping tests as EXAHYPE_SKIP_TESTS is set."
     "We do so because tests are broken in the moment and nobody repairs them."); //  TODO(Sven,Dominic,JM): Fix tests.
} // end if getenv(EXAHYPE_SKIP_TESTS)
#endif

  exahype::runners::Runner runner(parser);
  int programExitCode = runner.run();

  if (programExitCode == 0) {
#ifdef Parallel
    if (tarch::parallel::Node::getInstance().isGlobalMaster()) {
      logInfo("main()", "Peano terminates successfully");
    }
#else
    logInfo("main()", "Peano terminates successfully");
#endif
  } else {
    logInfo("main()", "quit with error code " << programExitCode);
  }

  peano::shutdownParallelEnvironment();
  peano::shutdownSharedMemoryEnvironment();
  peano::releaseCachedData();

  kernels::finalise();

  return programExitCode;
}


void exahype::version(const std::string& programname, std::ostream& out) {
  out << "This is " << programname << ", an ExaHyPE executable (http://exahype.eu)\n";
  out << "Compiled on host " << EXAHYPE_BUILD_HOST << " at " << EXAHYPE_BUILD_DATE << "\n";
#ifdef EXAHYPE_GIT_INFO
  out << "ExaHyPE git version: " << EXAHYPE_GIT_INFO << "\n";
#else
  out << "ExaHyPE git version: n/a\n";
#endif
#ifdef PEANO_SVN_INFO
  out << "Peano svn version:   " << PEANO_SVN_INFO << "\n";
#else
  out << "Peano svn version:   n/a\n";
#endif
  out << "\n";

  out << "Compile time options\n";
  out << "====================\n";
#ifdef DIMENSIONS
  out << "Dimensions:    "<< DIMENSIONS << "\n";
#else
  out << "Dimensions:    not determinable!\n";
#endif

#ifdef Debug
  out << "Debug:         YES\n";
#else
  out << "Debug:         no\n";
#endif
  
#ifdef Asserts
  out << "Assertions:    YES\n";
#else
  out << "Assertions:    no\n";
#endif

#ifdef Parallel
  out << "MPI Support:   YES\n";
#else
  out << "MPI Support:   no\n";
#endif
  
#ifdef EXAHYPE_CFL_FACTOR // issue #100
  out << "CFL Factor:    "<< EXAHYPE_CFL_FACTOR << "\n";
#else
  out << "CFL Factor:    Default\n";
#endif

  out << "\n";
  out << "Makesystem build options\n";
  out << "========================\n";
#ifdef EXAHYPE_BUILDINFO_AVAILABLE
  out << EXAHYPE_BUILD_INFO << "\n";
#else
  out << "Symbols n/a" << "\n";
#endif

  out << "\n";
  out << "Toolkit static registry info\n";
  out << "============================\n";
  kernels::toString(out);
  out << "\n";
}

void exahype::help(const std::string& programname) {
  std::cout << "Usage: " << programname << " <YourApplication.exahype>\n";
  std::cout << "\n";
  std::cout << "   where YourApplication.exahype is an ExaHyPE specification file.\n";
  std::cout << "   Note that you should have compiled ExaHyPE with this file as there\n";
  std::cout << "   are some compile time constants.\n";
  std::cout << "\n";
  std::cout << "   Other possible parameters:\n";
  std::cout << "\n";
  std::cout << "    --help     Show this help message\n";
  std::cout << "    --version  Show version and other hard coded information\n";
  std::cout << "\n";
}


#ifndef EXAHYPE_LATE_TAKEOVER

/**
 * By default, ExaHyPE provides the main function entrance of the program.
 * If you want to embed ExaHyPE however as an engine in some other program,
 * you can call the exahype::main(argc,argv) at any later time.
 *
 * Thus you can treat ExaHyPE similar to a GUI toolkit or game engine even loop.
 * To do so, just define EXAHYPE_LATE_TAKEOVER. Don't forget to start ExaHyPE
 * finally with calling exahype::main on yourself.
 **/
int main(int argc, char**argv) {
	exahype::main(argc, argv);
}

#endif
