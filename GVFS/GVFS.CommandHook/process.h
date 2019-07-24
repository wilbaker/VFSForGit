#pragma once

bool Process_IsElevated();

// TODO: Use PATH_STRING
std::string Process_Run(const std::string& processName, const std::string& args, bool redirectOutput);

bool Process_IsConsoleOutputRedirectedToFile();