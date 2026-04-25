#pragma once
/**
 * @file praxis_selftest.h
 * @brief Selftest subcommand dispatcher entry point.
 */

#include <wchar.h>

/**
 * @brief Run selftest from --selftest command-line. Allocates console as needed.
 * @param argc Argument count (total argc from wWinMain)
 * @param argv Argument vector (UTF-16 wide strings)
 * @return Exit code: 0=success, 1=assertion failed, 2=usage error
 */
int praxis_selftest_run(int argc, wchar_t **argv);
