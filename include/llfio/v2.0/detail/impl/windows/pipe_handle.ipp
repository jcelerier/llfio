/* A handle to a pipe
(C) 2015-2019 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
File Created: Nov 2019


Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License in the accompanying file
Licence.txt or at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


Distributed under the Boost Software License, Version 1.0.
    (See accompanying file Licence.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

#include "../../../pipe_handle.hpp"
#include "import.hpp"

LLFIO_V2_NAMESPACE_BEGIN

result<pipe_handle> pipe_handle::pipe(pipe_handle::path_view_type path, pipe_handle::mode _mode, pipe_handle::creation _creation, pipe_handle::caching _caching, pipe_handle::flag flags, const path_handle &base) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  flags &= ~flag::unlink_on_first_close;
  result<pipe_handle> ret(pipe_handle(native_handle_type(), 0, 0, _caching, flags, nullptr));
  native_handle_type &nativeh = ret.value()._v;
  LLFIO_LOG_FUNCTION_CALL(&ret);
  nativeh.behaviour |= native_handle_type::disposition::pipe;
  DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  OUTCOME_TRY(auto &&access, access_mask_from_handle_mode(nativeh, _mode, flags));
  OUTCOME_TRY(auto &&attribs, attributes_from_handle_caching_and_flags(nativeh, _caching, flags));
  nativeh.behaviour &= ~native_handle_type::disposition::seekable;  // not seekable
  if(creation::truncate_existing == _creation || creation::always_new == _creation || !base.is_valid())
  {
    return errc::operation_not_supported;
  }
  DWORD creatdisp = 0x00000001 /*FILE_OPEN*/;
  switch(_creation)
  {
  case creation::open_existing:
    break;
  case creation::only_if_not_exist:
    creatdisp = 0x00000002 /*FILE_CREATE*/;
    break;
  case creation::if_needed:
    creatdisp = 0x00000003 /*FILE_OPEN_IF*/;
    break;
  case creation::truncate_existing:
    creatdisp = 0x00000004 /*FILE_OVERWRITE*/;
    break;
  case creation::always_new:
    creatdisp = 0x00000000 /*FILE_SUPERSEDE*/;
    break;
  }
  if(mode::append == _mode)
  {
    access = SYNCHRONIZE | DELETE | GENERIC_WRITE;
  }

  attribs &= 0x00ffffff;  // the real attributes only, not the win32 flags
  OUTCOME_TRY(auto &&ntflags, ntflags_from_handle_caching_and_flags(nativeh, _caching, flags));
  ntflags &= ~0x00000008 /*FILE_NO_INTERMEDIATE_BUFFERING*/;  // pipes always buffer
  IO_STATUS_BLOCK isb = make_iostatus();

  path_view::not_zero_terminated_rendered_path<> zpath(path);
  UNICODE_STRING _path{};
  if(path.empty())
  {
    memset(&_path, 0, sizeof(_path));
  }
  else
  {
    _path.Buffer = const_cast<wchar_t *>(zpath.data());
    _path.MaximumLength = (_path.Length = static_cast<USHORT>(zpath.size() * sizeof(wchar_t))) + sizeof(wchar_t);
    if(zpath.size() >= 4 && _path.Buffer[0] == '\\' && _path.Buffer[1] == '!' && _path.Buffer[2] == '!' && _path.Buffer[3] == '\\')
    {
      _path.Buffer += 3;
      _path.Length -= 3 * sizeof(wchar_t);
      _path.MaximumLength -= 3 * sizeof(wchar_t);
    }
  }

  OBJECT_ATTRIBUTES oa{};
  memset(&oa, 0, sizeof(oa));
  oa.Length = sizeof(OBJECT_ATTRIBUTES);
  oa.ObjectName = &_path;
  oa.RootDirectory = base.native_handle().h;
  oa.Attributes = 0x40 /*OBJ_CASE_INSENSITIVE*/;
  // if(!!(flags & file_flags::int_opening_link))
  //  oa.Attributes|=0x100/*OBJ_OPENLINK*/;

  if(creation::open_existing == _creation)
  {
    for(;;)
    {
      LARGE_INTEGER AllocationSize{};
      memset(&AllocationSize, 0, sizeof(AllocationSize));
      NTSTATUS ntstat = NtCreateFile(&nativeh.h, access, &oa, &isb, &AllocationSize, attribs, fileshare, creatdisp, ntflags, nullptr, 0);
      if(STATUS_PENDING == ntstat)
      {
        ntstat = ntwait(nativeh.h, isb, deadline());
      }
      if(ntstat >= 0)
      {
        break;
      }
      // If writable and not readable, fail if other end is not connected
      // This matches full duplex pipe behaviour on Linux
      if(nativeh.is_readable() && nativeh.is_writable() && (NTSTATUS) 0xC00000AE /*STATUS_PIPE_BUSY*/ == ntstat)
      {
        return errc::no_such_device_or_address;  // ENXIO, as per Linux
      }
      if(nativeh.is_readable())
      {
        // assert(false);
        return ntkernel_error(ntstat);
      }
      // loop
    }
    ret.value()._v.behaviour |= native_handle_type::disposition::_is_connected;
  }
  else
  {
    fileshare &= ~FILE_SHARE_DELETE;            // ReactOS sources say this will be refused
    access |= DELETE;                           // read only pipes need to be able to rename
    access |= 4 /*FILE_CREATE_PIPE_INSTANCE*/;  // Allow creation of multiple instances

    LARGE_INTEGER default_timeout{};
    memset(&default_timeout, 0, sizeof(default_timeout));
    default_timeout.QuadPart = -500000;
    NTSTATUS ntstat = NtCreateNamedPipeFile(&nativeh.h, access, &oa, &isb, fileshare, creatdisp, ntflags, 0 /*FILE_PIPE_BYTE_STREAM_TYPE*/, 0 /*FILE_PIPE_BYTE_STREAM_MODE*/, 0 /*FILE_PIPE_QUEUE_OPERATION*/, (unsigned long) -1 /*FILE_PIPE_UNLIMITED_INSTANCES*/, 65536, 65536, &default_timeout);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(nativeh.h, isb, deadline());
    }
    if(ntstat < 0)
    {
      return ntkernel_error(ntstat);
    }

    // If creating a pipe, and this pipe is blocking, block until the other end opens
    if(!path.empty() && !nativeh.is_nonblocking())
    {
      // Opening blocking pipes must block until other end opens
      if(!ConnectNamedPipe(nativeh.h, nullptr) && ERROR_PIPE_CONNECTED != GetLastError())
      {
        return win32_error();
      }
      ret.value()._v.behaviour |= native_handle_type::disposition::_is_connected;
    }
  }
  return ret;
}

result<std::pair<pipe_handle, pipe_handle>> pipe_handle::anonymous_pipe(caching _caching, flag flags) noexcept
{
  // Uses true anonymous pipe creation technique from https://stackoverflow.com/questions/40844884/windows-named-pipe-access-control
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  // Create an unnamed new pipe
  flags &= ~flag::unlink_on_first_close;
  OUTCOME_TRY(auto &&anonpipe, pipe({}, mode::read, creation::only_if_not_exist, _caching, flags));
  std::pair<pipe_handle, pipe_handle> ret(std::move(anonpipe), pipe_handle(native_handle_type(), 0, 0, _caching, flags, nullptr));
  native_handle_type &readnativeh = ret.first._v, &writenativeh = ret.second._v;
  DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  OUTCOME_TRY(auto &&access, access_mask_from_handle_mode(writenativeh, mode::append, flags));
  access = SYNCHRONIZE | DELETE | GENERIC_WRITE;  // correct for pipes
  OUTCOME_TRY(auto &&attribs, attributes_from_handle_caching_and_flags(writenativeh, _caching, flags));
  writenativeh.behaviour |= native_handle_type::disposition::pipe;
  writenativeh.behaviour &= ~native_handle_type::disposition::seekable;  // not seekable
  LLFIO_LOG_FUNCTION_CALL(&ret.first);

  // Now clone the handle, but as a write privileged handle
  attribs &= 0x00ffffff;  // the real attributes only, not the win32 flags
  OUTCOME_TRY(auto &&ntflags, ntflags_from_handle_caching_and_flags(writenativeh, _caching, flags));
  ntflags &= ~0x00000008 /*FILE_NO_INTERMEDIATE_BUFFERING*/;  // pipes always buffer
  IO_STATUS_BLOCK isb = make_iostatus();

  UNICODE_STRING _path{};
  memset(&_path, 0, sizeof(_path));
  OBJECT_ATTRIBUTES oa{};
  memset(&oa, 0, sizeof(oa));
  oa.Length = sizeof(OBJECT_ATTRIBUTES);
  oa.ObjectName = &_path;
  oa.RootDirectory = readnativeh.h;
  oa.Attributes = 0x40 /*OBJ_CASE_INSENSITIVE*/;
  NTSTATUS ntstat = NtOpenFile(&writenativeh.h, access, &oa, &isb, fileshare, ntflags);
  if(STATUS_PENDING == ntstat)
  {
    ntstat = ntwait(writenativeh.h, isb, deadline());
  }
  if(ntstat < 0)
  {
    return ntkernel_error(ntstat);
  }
  return ret;
}

pipe_handle::io_result<pipe_handle::buffers_type> pipe_handle::_do_read(pipe_handle::io_request<pipe_handle::buffers_type> reqs, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  // If not connected, it'll be non-blocking, so connect now.
  if(!(_v.behaviour & native_handle_type::disposition::_is_connected))
  {
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    OVERLAPPED ol{};
    memset(&ol, 0, sizeof(ol));
    ol.Internal = static_cast<ULONG_PTR>(-1);
    if(!ConnectNamedPipe(_v.h, &ol) && ERROR_PIPE_CONNECTED != GetLastError())
    {
      if(ERROR_IO_PENDING != GetLastError())
      {
        return win32_error();
      }
      if(STATUS_TIMEOUT == ntwait(_v.h, ol, d))
      {
        // ntwait cancels timed out i/o
        return errc::timed_out;
      }
      // It seems the NT kernel is guilty of casting bugs sometimes
      ol.Internal = ol.Internal & 0xffffffff;
      if(ol.Internal != 0)
      {
        return ntkernel_error(static_cast<NTSTATUS>(ol.Internal));
      }
      if(d.steady)
      {
        auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>((began_steady + std::chrono::nanoseconds((d).nsecs)) - std::chrono::steady_clock::now());
        if(remaining.count() < 0)
        {
          remaining = std::chrono::nanoseconds(0);
        }
        d = deadline(remaining);
      }
    }
    _v.behaviour |= native_handle_type::disposition::_is_connected;
  }
  return io_handle::_do_read(reqs, d);
}

pipe_handle::io_result<pipe_handle::const_buffers_type> pipe_handle::_do_write(pipe_handle::io_request<pipe_handle::const_buffers_type> reqs, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  return io_handle::_do_write(reqs, d);
}

LLFIO_V2_NAMESPACE_END
