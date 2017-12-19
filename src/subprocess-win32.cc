// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "subprocess.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>

#include <process.h>

#include "util.h"

namespace {

HANDLE gCreateProcessSemaphore = NULL;
const int MAX_SEM_COUNT = 10;

}

Subprocess::Subprocess(bool use_console) : child_(NULL), thread_(NULL),
                                           overlapped_(),
                                           is_reading_(false),
                                           use_console_(use_console) {
}

Subprocess::~Subprocess() {
  if (pipe_) {
    if (!CloseHandle(pipe_))
      Win32Fatal("CloseHandle");
  }
  // Reap child if forgotten.
  if (child_)
    Finish();
}

HANDLE Subprocess::SetupPipe(HANDLE ioport) {
  char pipe_name[100];
  snprintf(pipe_name, sizeof(pipe_name),
           "\\\\.\\pipe\\ninja_pid%lu_sp%p", GetCurrentProcessId(), this);

  pipe_ = ::CreateNamedPipeA(pipe_name,
                             PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE,
                             PIPE_UNLIMITED_INSTANCES,
                             0, 0, INFINITE, NULL);
  if (pipe_ == INVALID_HANDLE_VALUE)
    Win32Fatal("CreateNamedPipe");

  if (!CreateIoCompletionPort(pipe_, ioport, (ULONG_PTR)this, 0))
    Win32Fatal("CreateIoCompletionPort");

  memset(&overlapped_, 0, sizeof(overlapped_));
  if (!ConnectNamedPipe(pipe_, &overlapped_) &&
      GetLastError() != ERROR_IO_PENDING) {
    Win32Fatal("ConnectNamedPipe");
  }

  // Get the write end of the pipe as a handle inheritable across processes.
  HANDLE output_write_handle =
      CreateFileA(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  HANDLE output_write_child;
  if (!DuplicateHandle(GetCurrentProcess(), output_write_handle,
                       GetCurrentProcess(), &output_write_child,
                       0, TRUE, DUPLICATE_SAME_ACCESS)) {
    Win32Fatal("DuplicateHandle");
  }
  CloseHandle(output_write_handle);

  return output_write_child;
}

static unsigned int _stdcall ThreadFunc(LPVOID lpParameter) {
  ((Subprocess*)lpParameter)->ExecThread();
  return 0;
}

bool Subprocess::Start(SubprocessSet* set, const string& command) {
  child_pipe_ = SetupPipe(set->ioport_);
  command_ = command;

  if (use_console_) {
    return ExecThread();
  }

  thread_ = (HANDLE)_beginthreadex(NULL, 0, ThreadFunc, (LPVOID)this, 0, NULL);

  if (thread_ == NULL)
    Win32Fatal("_beginthreadex");

  return true;
}

bool Subprocess::ExecThread() {
  SECURITY_ATTRIBUTES security_attributes;
  memset(&security_attributes, 0, sizeof(SECURITY_ATTRIBUTES));
  security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  security_attributes.bInheritHandle = TRUE;
  // Must be inheritable so subprocesses can dup to children.
  HANDLE nul =
      CreateFileA("NUL", GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  &security_attributes, OPEN_EXISTING, 0, NULL);
  if (nul == INVALID_HANDLE_VALUE)
    Fatal("couldn't open nul");

  STARTUPINFOEXA startup_info;
  memset(&startup_info, 0, sizeof(startup_info));
  startup_info.StartupInfo.cb = sizeof(startup_info);
  if (!use_console_) {
    startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup_info.StartupInfo.hStdInput = nul;
    startup_info.StartupInfo.hStdOutput = child_pipe_;
    startup_info.StartupInfo.hStdError = child_pipe_;
  }

  // Make only child_pipe_ and nul HANDLE inheritable to child process
  // in multi threaded process creation.
  SIZE_T list_size = 0;
  if (!InitializeProcThreadAttributeList(NULL, 1, 0, &list_size) &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    Win32Fatal("InitializeProcThreadAttributeList getsize");
  }

  std::vector<char> attr_buf(list_size);
  LPPROC_THREAD_ATTRIBUTE_LIST attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)(attr_buf.data());
  if (!InitializeProcThreadAttributeList(attr_list, 1, 0, &list_size)) {
   Win32Fatal("InitializeProcThreadAttributeList setsize");
  }

  HANDLE inherit_list[] = {child_pipe_, nul};

  if (!UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherit_list, sizeof(inherit_list),
                                 NULL, NULL)) {
    Win32Fatal("UpdateProcThreadAttribute");
  }

  startup_info.lpAttributeList = attr_list;

  // In the console case, child_pipe is still inherited by the child and closed
  // when the subprocess finishes, which then notifies ninja.

  PROCESS_INFORMATION process_info;
  memset(&process_info, 0, sizeof(process_info));

  // Ninja handles ctrl-c, except for subprocesses in console pools.
  DWORD process_flags = use_console_ ? 0 : CREATE_NEW_PROCESS_GROUP;
  process_flags |= EXTENDED_STARTUPINFO_PRESENT;

  // restrict the number of cuncurrent CreateProcess.
  WaitForSingleObject(gCreateProcessSemaphore, INFINITE);

  // Do not prepend 'cmd /c' on Windows, this breaks command
  // lines greater than 8,191 chars.
  if (!CreateProcessA(NULL, (char*)command_.c_str(), NULL, NULL,
                      /* inherit handles */ TRUE, process_flags,
                      NULL, NULL,
                      (STARTUPINFO*)&startup_info, &process_info)) {
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) {
      // File (program) not found error is treated as a normal build
      // action failure.
      if (child_pipe_)
        CloseHandle(child_pipe_);
      CloseHandle(pipe_);
      CloseHandle(nul);
      DeleteProcThreadAttributeList(attr_list);

      ReleaseSemaphore(gCreateProcessSemaphore, 1, NULL);

      pipe_ = NULL;
      // child_ is already NULL;
      buf_ = "CreateProcess failed: The system cannot find the file "
          "specified.\n";
      return true;
    } else if (error == ERROR_NOT_ENOUGH_MEMORY) {
      Win32Fatal("CreateProcess, You need to specify lower -j.");
    } else {
      Win32Fatal("CreateProcess");    // pass all other errors to Win32Fatal
    }
  }

  ReleaseSemaphore(gCreateProcessSemaphore, 1, NULL);
  DeleteProcThreadAttributeList(attr_list);

  // Close pipe channel only used by the child.
  if (child_pipe_)
    CloseHandle(child_pipe_);
  CloseHandle(nul);

  CloseHandle(process_info.hThread);
  child_ = process_info.hProcess;

  return true;
}

void Subprocess::OnPipeReady() {
  DWORD bytes;
  if (!GetOverlappedResult(pipe_, &overlapped_, &bytes, TRUE)) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      CloseHandle(pipe_);
      pipe_ = NULL;
      return;
    }
    Win32Fatal("GetOverlappedResult");
  }

  if (is_reading_ && bytes)
    buf_.append(overlapped_buf_, bytes);

  memset(&overlapped_, 0, sizeof(overlapped_));
  is_reading_ = true;
  if (!::ReadFile(pipe_, overlapped_buf_, sizeof(overlapped_buf_),
                  &bytes, &overlapped_)) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      CloseHandle(pipe_);
      pipe_ = NULL;
      return;
    }
    if (GetLastError() != ERROR_IO_PENDING)
      Win32Fatal("ReadFile");
  }

  // Even if we read any bytes in the readfile call, we'll enter this
  // function again later and get them at that point.
}

ExitStatus Subprocess::Finish() {
  if (!child_)
    return ExitFailure;

  // TODO: add error handling for all of these.
  if (thread_ != NULL) {
    WaitForSingleObject(thread_, INFINITE);
    CloseHandle(thread_);
    thread_ = NULL;
  }
  WaitForSingleObject(child_, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(child_, &exit_code);

  CloseHandle(child_);
  child_ = NULL;

  return exit_code == 0              ? ExitSuccess :
         exit_code == CONTROL_C_EXIT ? ExitInterrupted :
                                       ExitFailure;
}

bool Subprocess::Done() const {
  return pipe_ == NULL;
}

const string& Subprocess::GetOutput() const {
  return buf_;
}

HANDLE SubprocessSet::ioport_;

SubprocessSet::SubprocessSet() {
  ioport_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (!ioport_)
    Win32Fatal("CreateIoCompletionPort");
  if (!SetConsoleCtrlHandler(NotifyInterrupted, TRUE))
    Win32Fatal("SetConsoleCtrlHandler");

  gCreateProcessSemaphore = CreateSemaphore(
      NULL, MAX_SEM_COUNT, MAX_SEM_COUNT, NULL);
  if (gCreateProcessSemaphore == NULL) {
    Win32Fatal("CreateSemaphore");
  }
}

SubprocessSet::~SubprocessSet() {
  Clear();

  SetConsoleCtrlHandler(NotifyInterrupted, FALSE);
  CloseHandle(ioport_);
  CloseHandle(gCreateProcessSemaphore);
}

BOOL WINAPI SubprocessSet::NotifyInterrupted(DWORD dwCtrlType) {
  if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
    if (!PostQueuedCompletionStatus(ioport_, 0, 0, NULL))
      Win32Fatal("PostQueuedCompletionStatus");
    return TRUE;
  }

  return FALSE;
}

Subprocess *SubprocessSet::Add(const string& command, bool use_console) {
  Subprocess *subprocess = new Subprocess(use_console);
  if (!subprocess->Start(this, command)) {
    delete subprocess;
    return 0;
  }
  running_.push_back(subprocess);
  return subprocess;
}

bool SubprocessSet::DoWork() {
  DWORD bytes_read;
  Subprocess* subproc;
  OVERLAPPED* overlapped;

  if (!GetQueuedCompletionStatus(ioport_, &bytes_read, (PULONG_PTR)&subproc,
                                 &overlapped, INFINITE)) {
    if (GetLastError() != ERROR_BROKEN_PIPE)
      Win32Fatal("GetQueuedCompletionStatus");
  }

  if (!subproc) // A NULL subproc indicates that we were interrupted and is
                // delivered by NotifyInterrupted above.
    return true;

  subproc->OnPipeReady();

  if (subproc->Done()) {
    vector<Subprocess*>::iterator end =
        remove(running_.begin(), running_.end(), subproc);
    if (running_.end() != end) {
      finished_.push(subproc);
      running_.resize(end - running_.begin());
    }
  }

  return false;
}

Subprocess* SubprocessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  Subprocess* subproc = finished_.front();
  finished_.pop();
  return subproc;
}

void SubprocessSet::Clear() {
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    // Since the foreground process is in our process group, it will receive a
    // CTRL_C_EVENT or CTRL_BREAK_EVENT at the same time as us.
    if ((*i)->child_ && !(*i)->use_console_) {
      if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,
                                    GetProcessId((*i)->child_))) {
        Win32Fatal("GenerateConsoleCtrlEvent");
      }
    }
  }
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i)
    delete *i;
  running_.clear();
}
