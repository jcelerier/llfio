/* Information about a file
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (4 commits)
File Created: Apr 2017


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

#include "../../../handle.hpp"
#include "../../../stat.hpp"
#include "import.hpp"

#include <winioctl.h>  // for DeviceIoControl codes

LLFIO_V2_NAMESPACE_BEGIN

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<size_t> stat_t::fill(const handle &h, stat_t::want wanted) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(&h);
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  alignas(8) wchar_t buffer[32769];
  IO_STATUS_BLOCK isb = make_iostatus();
  NTSTATUS ntstat;
  size_t ret = 0;
  auto calculate_st_type = [&](ULONG FileAttributes, ULONG ReparsePointTag) -> result<filesystem::file_type>
  {
    // We need to get its reparse tag to see if it's a symlink
    if(((FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) && (ReparsePointTag == 0u))
    {
      alignas(8) char buffer_[sizeof(REPARSE_DATA_BUFFER) + 32769];
      DWORD written = 0;
      auto *rpd = reinterpret_cast<REPARSE_DATA_BUFFER *>(buffer_);
      memset(rpd, 0, sizeof(*rpd));
      if(DeviceIoControl(h.native_handle().h, FSCTL_GET_REPARSE_POINT, nullptr, 0, rpd, sizeof(buffer_), &written, nullptr) == 0)
      {
        return win32_error();
      }
      ReparsePointTag = rpd->ReparseTag;
    }
    return windows_nt_kernel::to_st_type(FileAttributes, ReparsePointTag);
  };

  FILE_FS_SECTOR_SIZE_INFORMATION ffssi{};
  memset(&ffssi, 0, sizeof(ffssi));
  if((wanted & want::blocks) || (wanted & want::blksize))
  {
    isb.Status = -1;
    ntstat = NtQueryVolumeInformationFile(h.native_handle().h, &isb, &ffssi, sizeof(ffssi), FileFsSectorSizeInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    }
    if(ntstat < 0)
    {
      return ntkernel_error(ntstat);
    }
  }

  // Firstly let's try the Windows 10 1709 syscall especially for us
  FILE_STAT_INFORMATION &fsi = *reinterpret_cast<FILE_STAT_INFORMATION *>(buffer);
  isb.Status = -1;
  ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &fsi, sizeof(buffer), FileStatInformation);
  if(STATUS_PENDING == ntstat)
  {
    ntstat = ntwait(h.native_handle().h, isb, deadline());
  }
  // Otherwise fall back onto legacy implementation
  if(ntstat < 0)
  {
    FILE_ALL_INFORMATION &fai = *reinterpret_cast<FILE_ALL_INFORMATION *>(buffer);
    bool needInternal = !!(wanted & want::ino);
    bool needBasic = (!!(wanted & want::type) || !!(wanted & want::atim) || !!(wanted & want::mtim) || !!(wanted & want::ctim) || !!(wanted & want::birthtim) ||
                      !!(wanted & want::sparse) || !!(wanted & want::compressed) || !!(wanted & want::reparse_point));
    bool needStandard = (!!(wanted & want::nlink) || !!(wanted & want::size) || !!(wanted & want::allocated) || !!(wanted & want::blocks));
    // It's not widely known that the NT kernel supplies a stat() equivalent i.e. get me everything in a single syscall
    // However fetching FileAlignmentInformation which comes with FILE_ALL_INFORMATION is slow as it touches the device driver,
    // so only use if we need more than one item
    if((static_cast<int>(needInternal) + static_cast<int>(needBasic) + static_cast<int>(needStandard)) >= 2)
    {
      ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &fai, sizeof(buffer), FileAllInformation);
      if(STATUS_PENDING == ntstat)
      {
        ntstat = ntwait(h.native_handle().h, isb, deadline());
      }
      if(ntstat < 0)
      {
        return ntkernel_error(ntstat);
      }
    }
    else
    {
      if(needInternal)
      {
        ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &fai.InternalInformation, sizeof(fai.InternalInformation), FileInternalInformation);
        if(STATUS_PENDING == ntstat)
        {
          ntstat = ntwait(h.native_handle().h, isb, deadline());
        }
        if(ntstat < 0)
        {
          return ntkernel_error(ntstat);
        }
      }
      if(needBasic)
      {
        isb.Status = -1;
        ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &fai.BasicInformation, sizeof(fai.BasicInformation), FileBasicInformation);
        if(STATUS_PENDING == ntstat)
        {
          ntstat = ntwait(h.native_handle().h, isb, deadline());
        }
        if(ntstat < 0)
        {
          return ntkernel_error(ntstat);
        }
      }
      if(needStandard)
      {
        isb.Status = -1;
        ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &fai.StandardInformation, sizeof(fai.StandardInformation), FileStandardInformation);
        if(STATUS_PENDING == ntstat)
        {
          ntstat = ntwait(h.native_handle().h, isb, deadline());
        }
        if(ntstat < 0)
        {
          return ntkernel_error(ntstat);
        }
      }
    }

    if(wanted & want::ino)
    {
      st_ino = fai.InternalInformation.IndexNumber.QuadPart;
      ++ret;
    }
    if(wanted & want::type)
    {
      OUTCOME_TRY(st_type, calculate_st_type(fai.BasicInformation.FileAttributes, fai.EaInformation.ReparsePointTag));
      ++ret;
    }
    if(wanted & want::nlink)
    {
      st_nlink = static_cast<int16_t>(fai.StandardInformation.NumberOfLinks);
      ++ret;
    }
    if(wanted & want::atim)
    {
      st_atim = to_timepoint(fai.BasicInformation.LastAccessTime);
      ++ret;
    }
    if(wanted & want::mtim)
    {
      st_mtim = to_timepoint(fai.BasicInformation.LastWriteTime);
      ++ret;
    }
    if(wanted & want::ctim)
    {
      st_ctim = to_timepoint(fai.BasicInformation.ChangeTime);
      ++ret;
    }
    if(wanted & want::size)
    {
      st_size = fai.StandardInformation.EndOfFile.QuadPart;
      ++ret;
    }
    if(wanted & want::allocated)
    {
      st_allocated = fai.StandardInformation.AllocationSize.QuadPart;
      ++ret;
    }
    if(wanted & want::blocks)
    {
      st_blocks = fai.StandardInformation.AllocationSize.QuadPart / ffssi.PhysicalBytesPerSectorForPerformance;
      ++ret;
    }
    if(wanted & want::blksize)
    {
      st_blksize = static_cast<uint16_t>(ffssi.PhysicalBytesPerSectorForPerformance);
      ++ret;
    }
    if(wanted & want::birthtim)
    {
      st_birthtim = to_timepoint(fai.BasicInformation.CreationTime);
      ++ret;
    }
    if(wanted & want::sparse)
    {
      st_sparse = static_cast<unsigned int>((fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0u);
      ++ret;
    }
    if(wanted & want::compressed)
    {
      st_compressed = static_cast<unsigned int>((fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0u);
      ++ret;
    }
    if(wanted & want::reparse_point)
    {
      st_reparse_point = static_cast<unsigned int>((fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u);
      ++ret;
    }
  }
  else
  {
    if(wanted & want::ino)
    {
      st_ino = fsi.FileId.QuadPart;
      ++ret;
    }
    if(wanted & want::type)
    {
      OUTCOME_TRY(st_type, calculate_st_type(fsi.FileAttributes, fsi.ReparseTag));
      ++ret;
    }
    if(wanted & want::nlink)
    {
      st_nlink = static_cast<int16_t>(fsi.NumberOfLinks);
      ++ret;
    }
    if(wanted & want::atim)
    {
      st_atim = to_timepoint(fsi.LastAccessTime);
      ++ret;
    }
    if(wanted & want::mtim)
    {
      st_mtim = to_timepoint(fsi.LastWriteTime);
      ++ret;
    }
    if(wanted & want::ctim)
    {
      st_ctim = to_timepoint(fsi.ChangeTime);
      ++ret;
    }
    if(wanted & want::size)
    {
      st_size = fsi.EndOfFile.QuadPart;
      ++ret;
    }
    if(wanted & want::allocated)
    {
      st_allocated = fsi.AllocationSize.QuadPart;
      ++ret;
    }
    if(wanted & want::blocks)
    {
      st_blocks = fsi.AllocationSize.QuadPart / ffssi.PhysicalBytesPerSectorForPerformance;
      ++ret;
    }
    if(wanted & want::blksize)
    {
      st_blksize = static_cast<uint16_t>(ffssi.PhysicalBytesPerSectorForPerformance);
      ++ret;
    }
    if(wanted & want::birthtim)
    {
      st_birthtim = to_timepoint(fsi.CreationTime);
      ++ret;
    }
    if(wanted & want::sparse)
    {
      st_sparse = static_cast<unsigned int>((fsi.FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0u);
      ++ret;
    }
    if(wanted & want::compressed)
    {
      st_compressed = static_cast<unsigned int>((fsi.FileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0u);
      ++ret;
    }
    if(wanted & want::reparse_point)
    {
      st_reparse_point = static_cast<unsigned int>((fsi.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u);
      ++ret;
    }
  }
  if((wanted & want::dev) || (wanted & want::ino))
  {
    /* The internal FileId isn't unique per stored file on some filing systems, which obviously breaks code which assumes
    inode values are unique per stored file rather than per named file.

    The filing system may store unique numbers per stored file, let's try fetching those.
    */
    bool still_need_dev = true, still_need_ino = true;

    // This is a Windows 10 or later API
    FILE_ID_INFORMATION &fii = *reinterpret_cast<FILE_ID_INFORMATION *>(buffer);
    memset(&fii.VolumeSerialNumber, 0, sizeof(fii.VolumeSerialNumber));
    memset(&fii.FileId, 0, sizeof(fii.FileId));
    isb.Status = -1;
    ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &fii, sizeof(buffer), FileIdInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    }
    if(ntstat >= 0)
    {
      st_dev = fii.VolumeSerialNumber;
      ++ret;
      still_need_dev = false;
      union
      {
        uint64_t ino;
        uint8_t bytes[8];
      };
      ino = 0;
      for(size_t n = 0; n < 16; n++)
      {
        if(fii.FileId.Identifier[n] != 0)
        {
          bytes[n & 7] ^= fii.FileId.Identifier[n];
          still_need_ino = false;
        }
      }
      if(!still_need_ino)
      {
        st_ino = ino;
      }
    }
    if(still_need_dev)
    {
      // This is a bit hacky, but we just need a unique device number
      alignas(8) wchar_t buffer_[32769];
      DWORD len = GetFinalPathNameByHandleW(h.native_handle().h, buffer_, sizeof(buffer_) / sizeof(*buffer_), VOLUME_NAME_NT);
      if((len == 0u) || len >= sizeof(buffer_) / sizeof(*buffer_))
      {
        return win32_error();
      }
      buffer_[len] = 0;
      const bool is_harddisk = (memcmp(buffer_, L"\\Device\\HarddiskVolume", 44) == 0);
      const bool is_unc = (memcmp(buffer_, L"\\Device\\Mup", 22) == 0);
      if(!is_harddisk && !is_unc)
      {
        return errc::illegal_byte_sequence;
      }
      if(is_harddisk)
      {
        // buffer_ should look like \Device\HarddiskVolumeX, so our number is from +22 onwards
        st_dev = _wtoi(buffer_ + 22);
        ++ret;
      }
    }
    if(still_need_ino)
    {
      // Should be good back to Windows 8
      FILE_OBJECTID_INFORMATION &foii = *reinterpret_cast<FILE_OBJECTID_INFORMATION *>(buffer);
      isb.Status = -1;
      ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &foii, sizeof(buffer), FileObjectIdInformation);
      if(STATUS_PENDING == ntstat)
      {
        ntstat = ntwait(h.native_handle().h, isb, deadline());
      }
      if(ntstat >= 0)
      {
        st_ino = foii.FileReference;
      }
    }
  }
  return ret;
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<stat_t::want> stat_t::stamp(handle &h, stat_t::want wanted) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(&h);
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  IO_STATUS_BLOCK isb = make_iostatus();
  NTSTATUS ntstat;
  // Filter out the flags we don't support
  wanted &= (want::atim | want::mtim | want::birthtim);
  if(!wanted)
  {
    return wanted;
  }
  FILE_BASIC_INFORMATION fbi;
  isb.Status = -1;
  ntstat = NtQueryInformationFile(h.native_handle().h, &isb, &fbi, sizeof(fbi), FileBasicInformation);
  if(STATUS_PENDING == ntstat)
  {
    ntstat = ntwait(h.native_handle().h, isb, deadline());
  }
  if(ntstat < 0)
  {
    return ntkernel_error(ntstat);
  }
  // Set what we are changing, zeroing those elements we are not changing
  fbi.ChangeTime.QuadPart = 0;  // will be reset when we write this anyway
  fbi.LastAccessTime = (wanted & want::atim) ? from_timepoint(st_atim) : fbi.ChangeTime;
  fbi.LastWriteTime = (wanted & want::mtim) ? from_timepoint(st_mtim) : fbi.ChangeTime;
  fbi.CreationTime = (wanted & want::birthtim) ? from_timepoint(st_birthtim) : fbi.ChangeTime;
  isb.Status = -1;
  ntstat = NtSetInformationFile(h.native_handle().h, &isb, &fbi, sizeof(fbi), FileBasicInformation);
  if(STATUS_PENDING == ntstat)
  {
    ntstat = ntwait(h.native_handle().h, isb, deadline());
  }
  if(ntstat < 0)
  {
    return ntkernel_error(ntstat);
  }
  return wanted;
}

LLFIO_V2_NAMESPACE_END
