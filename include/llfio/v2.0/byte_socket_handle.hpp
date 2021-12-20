/* A handle to a byte-orientated socket
(C) 2021-2021 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
File Created: Dec 2021


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

#ifndef LLFIO_BYTE_SOCKET_HANDLE_H
#define LLFIO_BYTE_SOCKET_HANDLE_H

#include "byte_io_handle.hpp"

struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;

//! \file byte_socket_handle.hpp Provides `byte_socket_handle`.

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)  // nameless struct/union
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

class byte_socket_handle;
class listening_socket_handle;

namespace detail
{
#ifdef _WIN32
  LLFIO_HEADERS_ONLY_FUNC_SPEC void register_socket_handle_instance(void *i) noexcept;
  LLFIO_HEADERS_ONLY_FUNC_SPEC void unregister_socket_handle_instance(void *i) noexcept;
#endif
}  // namespace detail

//! Inspired by ASIO's `ip` namespace
namespace ip
{
  /*! \class address
  \brief A version independent IP address.

  This is pretty close to `asio::ip::address`, but it also adds `port()` from `asio::ip::endpoint`
  and a few other observer member functions i.e. it fuses ASIO's many types into one.

  The reason why is that this type is a simple wrap of `struct sockaddr_in` or `struct sockaddr_in6`,
  it doesn't split those structures.
  */
  class LLFIO_DECL address
  {
    friend class byte_socket_handle;
    friend class listening_socket_handle;
    friend LLFIO_HEADERS_ONLY_FUNC_SPEC std::ostream &operator<<(std::ostream &s, const address &v);

  protected:
    union
    {
      struct
      {
        unsigned short _family;  // sa_family_t
        uint16_t _port;          // in_port_t
        union
        {
          struct
          {
            uint32_t _flowinfo{0};
            byte _addr[16]{(byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0,
                           (byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0, (byte) 0};
            uint32_t _scope_id{0};
          } ipv6;
          union
          {
            byte _addr[4];
            uint32_t _addr_be;
          } ipv4;
        };
      };
      byte _storage[32];  // struct sockaddr_?
    };

  public:
    constexpr address() noexcept
        : _family(0)
        , _port(0)
        , ipv4{}
    {
    }
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC explicit address(const sockaddr_in &storage) noexcept;
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC explicit address(const sockaddr_in6 &storage) noexcept;
    address(const address &) = default;
    address(address &&) = default;
    address &operator=(const address &) = default;
    address &operator=(address &&) = default;
    ~address() = default;

    //! True if addresses are equal
    bool operator==(const address &o) const noexcept { return 0 == memcmp(_storage, o._storage, sizeof(_storage)); }
    //! True if addresses are not equal
    bool operator!=(const address &o) const noexcept { return 0 != memcmp(_storage, o._storage, sizeof(_storage)); }
    //! True if address is less than
    bool operator<(const address &o) const noexcept { return memcmp(_storage, o._storage, sizeof(_storage)) < 0; }

    //! True if address is loopback
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC bool is_loopback() const noexcept;
    //! True if address is multicast
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC bool is_multicast() const noexcept;
    //! True if address is unspecified
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC bool is_unspecified() const noexcept;
    //! True if address is v4
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC bool is_v4() const noexcept;
    //! True if address is v6
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC bool is_v6() const noexcept;

    //! Returns the raw family of the address
    unsigned short family() const noexcept { return _family; }
    //! Returns the port of the address
    uint16_t port() const noexcept { return _port; }
    //! Returns the IPv6 flow info, if address is v6.
    uint32_t flowinfo() const noexcept { return is_v6() ? ipv6._flowinfo : 0; }
    //! Returns the IPv6 scope id, if address is v6.
    uint32_t scope_id() const noexcept { return is_v6() ? ipv6._scope_id : 0; }

    //! Returns the bytes of the address in network order
    span<const byte> as_bytes() const noexcept { return is_v6() ? span<const byte>(ipv6._addr) : span<const byte>(ipv4._addr); }
    //! Returns the address as a `sockaddr *`.
    const sockaddr *as_sockaddr() const noexcept { return (const sockaddr *) _storage; }
    //! Returns the size of the `sockaddr`
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC int sockaddrlen() const noexcept;
  };
  //! Write address to stream
  LLFIO_HEADERS_ONLY_FUNC_SPEC std::ostream &operator<<(std::ostream &s, const address &v);
  /*! \class address_v4
  \brief A v4 IP address.
  */
  class LLFIO_DECL address_v4 : public address
  {
    friend LLFIO_HEADERS_ONLY_FUNC_SPEC result<address_v4> make_address_v4(string_view str) noexcept;

  public:
#if QUICKCPPLIB_USE_STD_SPAN
    using bytes_type = span<const byte, 4>;
#else
    using bytes_type = span<const byte>;
#endif
    using uint_type = uint32_t;
    constexpr address_v4() noexcept {}
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC explicit address_v4(const bytes_type &bytes, uint16_t port = 0) noexcept;
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC explicit address_v4(uint_type addr, uint16_t port = 0) noexcept;
    address_v4(const address_v4 &) = default;
    address_v4(address_v4 &&) = default;
    address_v4 &operator=(const address_v4 &) = default;
    address_v4 &operator=(address_v4 &&) = default;
    ~address_v4() = default;

    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC bytes_type to_bytes() const noexcept;
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC uint_type to_uint() const noexcept;

    static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC address_v4 any() noexcept;
    static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC address_v4 loopback() noexcept;
  };
  static_assert(std::is_trivially_copyable<address>::value, "ip::address is not trivially copyable!");
  //! Make an `address_v4`
  inline result<address_v4> make_address_v4(const address_v4::bytes_type &bytes, uint16_t port = 0) noexcept { return address_v4(bytes, port); }
  //! Make an `address_v4`
  inline result<address_v4> make_address_v4(const address_v4::uint_type &bytes, uint16_t port = 0) noexcept { return address_v4(bytes, port); }
  //! Make an `address_v4`
  LLFIO_HEADERS_ONLY_FUNC_SPEC result<address_v4> make_address_v4(string_view str) noexcept;
  /*! \class address_v6
  \brief A v6 IP address.
  */
  class LLFIO_DECL address_v6 : public address
  {
    friend LLFIO_HEADERS_ONLY_FUNC_SPEC result<address_v6> make_address_v6(string_view str) noexcept;

  public:
#if QUICKCPPLIB_USE_STD_SPAN
    using bytes_type = span<const byte, 16>;
#else
    using bytes_type = span<const byte>;
#endif

    constexpr address_v6() noexcept {}
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC explicit address_v6(const bytes_type &bytes, uint16_t port = 0, uint32_t scope_id = 0) noexcept;
    address_v6(const address_v6 &) = default;
    address_v6(address_v6 &&) = default;
    address_v6 &operator=(const address_v6 &) = default;
    address_v6 &operator=(address_v6 &&) = default;
    ~address_v6() = default;

    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC bytes_type to_bytes() const noexcept;

    static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC address_v6 any() noexcept;
    static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC address_v6 loopback() noexcept;
  };
  //! Make an `address_v6`
  inline result<address_v6> make_address_v6(const address_v6::bytes_type &bytes, uint16_t port = 0, uint32_t scope_id = 0) noexcept
  {
    return address_v6(bytes, port, scope_id);
  }
  //! Make an `address_v6`
  LLFIO_HEADERS_ONLY_FUNC_SPEC result<address_v6> make_address_v6(string_view str) noexcept;
}  // namespace ip

/*! \class byte_socket_handle
\brief A handle to a byte-orientated socket-like entity.

This handle, or subclasses thereof, may refer to:

- a BSD socket in the kernel configured for TCP.
- a TLS socket in a userspace library.
- a userspace socket for certain types of high end network card.
- or indeed, anything which quacks like a `SOCK_STREAM` socket.

If you construct it directly and assign it a socket that you created,
then it refers to a kernel BSD socket, as the default implementation
is for a kernel BSD socket. If you get an instance from elsewhere,
it may have a *very* different implementation.

The default is blocking sockets, on which timed out i/o is not possible.
In this use case, `byte_socket()` will block until a successful
connection is established with the remote address. Thereafter `read()`
and `write()` block based on i/o from the other side, returning
immediately if at least one byte is transferred.

If `flag::multiplexable` is specified which causes the handle to
be created as `native_handle_type::disposition::nonblocking`, `byte_socket()`
no longer blocks. However it will then block in `read()` or `write()`,
unless its deadline is zero.

If you want to create a socket which awaits connections, you need
to instance a `listening_socket_handle`. Reads from that handle yield
new `byte_socket_handle` instances.
*/
class LLFIO_DECL byte_socket_handle : public byte_io_handle
{
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC const handle &_get_handle() const noexcept final { return *this; }

public:
  using path_type = byte_io_handle::path_type;
  using extent_type = byte_io_handle::extent_type;
  using size_type = byte_io_handle::size_type;
  using mode = byte_io_handle::mode;
  using creation = byte_io_handle::creation;
  using caching = byte_io_handle::caching;
  using flag = byte_io_handle::flag;
  using buffer_type = byte_io_handle::buffer_type;
  using const_buffer_type = byte_io_handle::const_buffer_type;
  using buffers_type = byte_io_handle::buffers_type;
  using const_buffers_type = byte_io_handle::const_buffers_type;
  template <class T> using io_request = byte_io_handle::io_request<T>;
  template <class T> using io_result = byte_io_handle::io_result<T>;

public:
  //! Default constructor
  constexpr byte_socket_handle() {}  // NOLINT
  //! Construct a handle from a supplied native handle
  constexpr byte_socket_handle(native_handle_type h, caching caching, flag flags, byte_io_multiplexer *ctx)
      : byte_io_handle(std::move(h), caching, flags, ctx)
  {
#ifdef _WIN32
    if(_v)
    {
      detail::register_socket_handle_instance(this);
    }
#endif
  }
  //! No copy construction (use clone())
  byte_socket_handle(const byte_socket_handle &) = delete;
  //! No copy assignment
  byte_socket_handle &operator=(const byte_socket_handle &) = delete;
  //! Implicit move construction of byte_socket_handle permitted
  constexpr byte_socket_handle(byte_socket_handle &&o) noexcept
      : byte_io_handle(std::move(o))
  {
#ifdef _WIN32
    if(_v)
    {
      detail::register_socket_handle_instance(this);
      detail::unregister_socket_handle_instance(&o);
    }
#endif
  }
  //! Explicit conversion from handle permitted
  explicit constexpr byte_socket_handle(handle &&o, byte_io_multiplexer *ctx) noexcept
      : byte_io_handle(std::move(o), ctx)
  {
#ifdef _WIN32
    if(_v)
    {
      detail::register_socket_handle_instance(this);
    }
#endif
  }
  //! Explicit conversion from byte_io_handle permitted
  explicit constexpr byte_socket_handle(byte_io_handle &&o) noexcept
      : byte_io_handle(std::move(o))
  {
#ifdef _WIN32
    if(_v)
    {
      detail::register_socket_handle_instance(this);
    }
#endif
  }
  //! Move assignment of byte_socket_handle permitted
  byte_socket_handle &operator=(byte_socket_handle &&o) noexcept
  {
    if(this == &o)
    {
      return *this;
    }
#ifdef _WIN32
    if(_v)
    {
      detail::unregister_socket_handle_instance(this);
    }
#endif
    this->~byte_socket_handle();
    new(this) byte_socket_handle(std::move(o));
    return *this;
  }
  //! Swap with another instance
  LLFIO_MAKE_FREE_FUNCTION
  void swap(byte_socket_handle &o) noexcept
  {
    byte_socket_handle temp(std::move(*this));
    *this = std::move(o);
    o = std::move(temp);
  }

  //! Returns the local endpoint of this socket instance
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<ip::address> local_endpoint() const noexcept;
  //! Returns the remote endpoint of this socket instance
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<ip::address> remote_endpoint() const noexcept;

  /*! Create a socket handle connecting to a specified address.
  \param address The address to connect to.
  \param _mode How to open the socket. If this is `mode::append`, the read side of the socket
  is shutdown; if this is `mode::read`, the write side of the socket is shutdown.
  \param _caching How to ask the kernel to cache the socket. If writes are not cached,
  `SO_SNDBUF` to the minimum possible value and `TCP_NODELAY` is set, this should cause
  writes to hit the network as quickly as possible.
  \param flags Any additional custom behaviours.

  \errors Any of the values POSIX `socket()` or `WSASocket()` can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<byte_socket_handle> byte_socket(const ip::address &addr, mode _mode = mode::write,
                                                                                caching _caching = caching::all, flag flags = flag::none) noexcept;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC ~byte_socket_handle() override
  {
    if(_v)
    {
      (void) byte_socket_handle::close();
    }
  }
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> close() noexcept override
  {
    LLFIO_LOG_FUNCTION_CALL(this);
#ifndef NDEBUG
    if(_v)
    {
      // Tell handle::close() that we have correctly executed
      _v.behaviour |= native_handle_type::disposition::_child_close_executed;
    }
#endif
#ifdef _WIN32
    if(_v)
    {
      detail::unregister_socket_handle_instance(this);
    }
#endif
    return byte_io_handle::close();
  }
};

//! \brief Constructor for `byte_socket_handle`
template <> struct construct<byte_socket_handle>
{
  const ip::address &addr;
  byte_socket_handle::mode _mode = byte_socket_handle::mode::write;
  byte_socket_handle::caching _caching = byte_socket_handle::caching::all;
  byte_socket_handle::flag flags = byte_socket_handle::flag::none;
  result<byte_socket_handle> operator()() const noexcept { return byte_socket_handle::byte_socket(addr, _mode, _caching, flags); }
};

/* \class listening_socket_handle
\brief A handle to a socket-like entity able to receive incoming connections.
*/
class LLFIO_DECL listening_socket_handle : public handle
{
protected:
  byte_io_multiplexer *_ctx{nullptr};  // +4 or +8 bytes
public:
  //! The buffer type used by this handle, which is a pair of `byte_socket_handle` and `ip::address`
  using buffer_type = std::pair<byte_socket_handle, ip::address>;
  //! The const buffer type used by this handle, which is a pair of `byte_socket_handle` and `ip::address`
  using const_buffer_type = std::pair<byte_socket_handle, ip::address>;
  //! The buffers type used by this handle for reads, which is a single item sequence of `buffer_type`.
  struct buffers_type
  {
    //! Type of the pointer to the buffer.
    using pointer = std::pair<byte_socket_handle, ip::address> *;
    //! Type of the iterator to the buffer.
    using iterator = std::pair<byte_socket_handle, ip::address> *;
    //! Type of the iterator to the buffer.
    using const_iterator = const std::pair<byte_socket_handle, ip::address> *;
    //! Type of the length of the buffers.
    using size_type = size_t;

    //! Default constructor
    constexpr buffers_type() {}  // NOLINT

    /*! Constructor
     */
    constexpr buffers_type(std::pair<byte_socket_handle, ip::address> &sock)
        : _sock(&sock)
    {
    }
    ~buffers_type() = default;
    //! Move constructor
    buffers_type(buffers_type &&o) noexcept
        : _sock(o._sock)
    {
      o._sock = nullptr;
    }
    //! No copy construction
    buffers_type(const buffers_type &) = delete;
    //! Move assignment
    buffers_type &operator=(buffers_type &&o) noexcept
    {
      if(this == &o)
      {
        return *this;
      }
      this->~buffers_type();
      new(this) buffers_type(std::move(o));
      return *this;
    }
    //! No copy assignment
    buffers_type &operator=(const buffers_type &) = delete;

    //! True if empty
    LLFIO_NODISCARD constexpr bool empty() const noexcept { return _sock == nullptr; }
    //! Returns an iterator to the beginning of the buffers
    constexpr iterator begin() noexcept { return _sock; }
    //! Returns an iterator to the beginning of the buffers
    constexpr const_iterator begin() const noexcept { return _sock; }
    //! Returns an iterator to the beginning of the buffers
    constexpr const_iterator cbegin() const noexcept { return _sock; }
    //! Returns an iterator to after the end of the buffers
    constexpr iterator end() noexcept { return _sock + 1; }
    //! Returns an iterator to after the end of the buffers
    constexpr const_iterator end() const noexcept { return _sock + 1; }
    //! Returns an iterator to after the end of the buffers
    constexpr const_iterator cend() const noexcept { return _sock + 1; }

    //! The socket referenced by the buffers
    const buffer_type &connected_socket() const &noexcept
    {
      assert(_sock != nullptr);
      return *_sock;
    }
    //! The socket referenced by the buffers
    buffer_type &connected_socket() &noexcept
    {
      assert(_sock != nullptr);
      return *_sock;
    }
    //! The socket and its connected address referenced by the buffers
    buffer_type connected_socket() &&noexcept
    {
      assert(_sock != nullptr);
      return std::move(*_sock);
    }

  private:
    std::pair<byte_socket_handle, ip::address> *_sock{nullptr};
  };
  //! The const buffers type used by this handle for reads, which is a single item sequence of `buffer_type`.
  using const_buffers_type = buffers_type;
  //! The i/o request type used by this handle.
  template <class /*unused*/> struct io_request
  {
    buffers_type buffers{};

    /*! Construct a request to listen for new socket connections.

    \param _buffers The buffers to fill with connected sockets.
    */
    constexpr io_request(buffers_type _buffers)
        : buffers(std::move(_buffers))
    {
    }
  };
  template <class T> using io_result = result<T>;

public:
  //! Default constructor
  constexpr listening_socket_handle() {}  // NOLINT
  //! Construct a handle from a supplied native handle
  constexpr listening_socket_handle(native_handle_type h, caching caching, flag flags, byte_io_multiplexer *ctx)
      : handle(std::move(h), caching, flags)
      , _ctx(ctx)
  {
#ifdef _WIN32
    if(_v)
    {
      detail::register_socket_handle_instance(this);
    }
#endif
  }
  //! No copy construction (use clone())
  listening_socket_handle(const listening_socket_handle &) = delete;
  //! No copy assignment
  listening_socket_handle &operator=(const listening_socket_handle &) = delete;
  //! Implicit move construction of byte_socket_handle permitted
  constexpr listening_socket_handle(listening_socket_handle &&o) noexcept
      : handle(std::move(o))
      , _ctx(o._ctx)
  {
#ifdef _WIN32
    if(_v)
    {
      detail::register_socket_handle_instance(this);
      detail::unregister_socket_handle_instance(&o);
    }
#endif
  }
  //! Explicit conversion from handle permitted
  explicit constexpr listening_socket_handle(handle &&o, byte_io_multiplexer *ctx) noexcept
      : handle(std::move(o))
      , _ctx(ctx)
  {
#ifdef _WIN32
    if(_v)
    {
      detail::register_socket_handle_instance(this);
    }
#endif
  }
  //! Move assignment of listening_socket_handle permitted
  listening_socket_handle &operator=(listening_socket_handle &&o) noexcept
  {
    if(this == &o)
    {
      return *this;
    }
#ifdef _WIN32
    if(_v)
    {
      detail::unregister_socket_handle_instance(this);
    }
#endif
    this->~listening_socket_handle();
    new(this) listening_socket_handle(std::move(o));
    return *this;
  }
  //! Swap with another instance
  LLFIO_MAKE_FREE_FUNCTION
  void swap(listening_socket_handle &o) noexcept
  {
    listening_socket_handle temp(std::move(*this));
    *this = std::move(o);
    o = std::move(temp);
  }

  /*! \brief The i/o multiplexer this handle will use to multiplex i/o. If this returns null,
  then this handle has not been registered with an i/o multiplexer yet.
  */
  byte_io_multiplexer *multiplexer() const noexcept { return _ctx; }

  /*! \brief Sets the i/o multiplexer this handle will use to implement `read()`, `write()` and `barrier()`.

  Note that this call deregisters this handle from any existing i/o multiplexer, and registers
  it with the new i/o multiplexer. You must therefore not call it if any i/o is currently
  outstanding on this handle. You should also be aware that multiple dynamic memory
  allocations and deallocations may occur, as well as multiple syscalls (i.e. this is
  an expensive call, try to do it from cold code).

  If the handle was not created as multiplexable, this call always fails.

  \mallocs Multiple dynamic memory allocations and deallocations.
  */
  virtual result<void> set_multiplexer(byte_io_multiplexer *c = this_thread::multiplexer()) noexcept;  // implementation is below

  //! Returns the local endpoint of this socket instance
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<ip::address> local_endpoint() const noexcept;

  /*! \brief Binds a socket to a local endpoint and sets the socket to listen for new connections.
  \param addr The local endpoint to which to bind the socket.
  \param _creation Whether to apply `SO_REUSEADDR` before binding.
  \param backlog The maximum queue length of pending connections. `-1` chooses `SOMAXCONN`.

  You should set any socket options etc that you need on `native_handle()` before binding
  the socket to its local endpoint.

  \errors Any of the values `bind()` and `listen()` can return.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> bind(const ip::address &addr, creation _creation = creation::only_if_not_exist, int backlog = -1) noexcept;

  /*! Create a listening socket handle.
  \param _mode How to open the socket. If this is `mode::append`, the read side of the socket
  is shutdown; if this is `mode::read`, the write side of the socket is shutdown.
  \param _caching How to ask the kernel to cache the socket. If writes are not cached,
  `SO_SNDBUF` to the minimum possible value and `TCP_NODELAY` is set, this should cause
  writes to hit the network as quickly as possible.
  \param flags Any additional custom behaviours.

  \errors Any of the values POSIX `socket()` or `WSASocket()` can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<listening_socket_handle> listening_socket(bool use_ipv6 = true, mode _mode = mode::write,
                                                                                          caching _caching = caching::all, flag flags = flag::none) noexcept;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC ~listening_socket_handle() override
  {
    if(_v)
    {
      (void) listening_socket_handle::close();
    }
  }
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> close() noexcept override
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(_ctx != nullptr)
    {
      OUTCOME_TRY(set_multiplexer(nullptr));
    }
#ifndef NDEBUG
    if(_v)
    {
      // Tell handle::close() that we have correctly executed
      _v.behaviour |= native_handle_type::disposition::_child_close_executed;
    }
#endif
#ifdef _WIN32
    if(_v)
    {
      detail::unregister_socket_handle_instance(this);
    }
#endif
    return handle::close();
  }

  /*! Read the contents of the listening socket for newly connected byte sockets.

  \return Returns the buffers filled, with its socket handle and address set to the newly connected socket.
  \param req A buffer to fill with a newly connected socket.
  \param d An optional deadline by which to time out.

  \errors Any of the errors which `accept()` or `WSAAccept()` might return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<buffers_type> read(io_request<buffers_type> req, deadline d = {}) const noexcept;
};

// Out of line definition purely to work around a bug in GCC where if marked inline,
// its visibility is hidden and links fail
inline result<void> listening_socket_handle::set_multiplexer(byte_io_multiplexer *c) noexcept
{
  if(!is_multiplexable())
  {
    return errc::operation_not_supported;
  }
  if(c == _ctx)
  {
    return success();
  }
  if(_ctx != nullptr)
  {
    // OUTCOME_TRY(_ctx->do_byte_io_handle_deregister(this));
    _ctx = nullptr;
  }
  if(c != nullptr)
  {
    /* OUTCOME_TRY(auto &&state, c->do_byte_io_handle_register(this));
    _v.behaviour = (_v.behaviour & ~(native_handle_type::disposition::_multiplexer_state_bit0 | native_handle_type::disposition::_multiplexer_state_bit1));
    if((state & 1) != 0)
    {
      _v.behaviour |= native_handle_type::disposition::_multiplexer_state_bit0;
    }
    if((state & 2) != 0)
    {
      _v.behaviour |= native_handle_type::disposition::_multiplexer_state_bit1;
    }
    */
  }
  _ctx = c;
  return success();
}

//! \brief Constructor for `listening_socket_handle`
template <> struct construct<listening_socket_handle>
{
  bool use_ipv6{true};
  byte_socket_handle::mode _mode = byte_socket_handle::mode::write;
  byte_socket_handle::caching _caching = byte_socket_handle::caching::all;
  byte_socket_handle::flag flags = byte_socket_handle::flag::none;
  result<listening_socket_handle> operator()() const noexcept { return listening_socket_handle::listening_socket(use_ipv6, _mode, _caching, flags); }
};


// BEGIN make_free_functions.py
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#include "detail/impl/byte_socket_handle.ipp"
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
