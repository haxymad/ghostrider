/*
 * winsock_compat.h — forced include via /FI.
 *
 * Loads winsock2.h first in every TU, ensuring the full winsock
 * type chain (sockaddr, ULONG, ADDRESS_FAMILY, etc.) is available
 * before any project headers or ws2tcpip.h process.
 */
#include <winsock2.h>
