#pragma once
#include "common.h"

bool Messages_ReadTerminatedMessageFromGVFS(PIPE_HANDLE pipeHandle, std::string& responseMessage);