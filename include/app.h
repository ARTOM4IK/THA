#pragma once

#include "types.h"

int runServerMode(int argc, char **argv);
int runClientMode(Role role, int argc, char **argv);
int runSimulation();
void printUsage();
