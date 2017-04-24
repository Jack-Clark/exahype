#ifndef EXAHYPE_MAIN_FILE
#define EXAHYPE_MAIN_FILE

#include <string>

namespace exahype {
	void pingPoingTest();

	/**
	 * Dump Version information and similiar information about compile time constants
	 * in this binary.
	 * 
	 * @param programname should be the value of argv[0], ie. the caller name. Is just
	 *                    used for beautiful output.
	 * @param out         is the strem where to print to, default stdout.
	 * 
	 **/
	void version(const std::string& programname, std::ostream& out=std::cout);
	
	/**
	 * Prints a help message how to use the ExaHyPE executable to stdout.
	 **/
	void help(const std::string& programname);
	
	/**
	 * The ExaHyPE main entrance. From here there is no way back. Peano will take control
	 * over the program flow.
	 * 
	 * This will expect a spec file as the first argument.
	 **/
	int main(int argc, char** argv);
}

int main(int argc, char** argv);


#endif /* EXAHYPE_MAIN_FILE_ */
