// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenEXR Project.

// This file is auto-generated by the cmake configure step

#ifndef INCLUDED_ILMBASE_CONFIG_H
#define INCLUDED_ILMBASE_CONFIG_H 1

#pragma once

//
// Options / configuration based on O.S. / compiler
/////////////////////

#define HAVE_PTHREAD 1
#define HAVE_POSIX_SEMAPHORES 1

//
// Define and set to 1 if the target system has support for large
// stack sizes.
//
/* #undef ILMBASE_HAVE_LARGE_STACK */

//////////////////////
//
// C++ namespace configuration / options

// Current (internal) library namepace name and corresponding public
// client namespaces.
#define ILMBASE_INTERNAL_NAMESPACE_CUSTOM 0
#define IMATH_INTERNAL_NAMESPACE Imath_2_4
#define IEX_INTERNAL_NAMESPACE Iex_2_4
#define ILMTHREAD_INTERNAL_NAMESPACE IlmThread_2_4

#define ILMBASE_NAMESPACE_CUSTOM 0
#define IMATH_NAMESPACE Imath
#define IEX_NAMESPACE Iex
#define ILMTHREAD_NAMESPACE IlmThread

//
// Version information
//
#define ILMBASE_VERSION_STRING "2.4.1"
#define ILMBASE_PACKAGE_STRING "IlmBase 2.4.1"

#define ILMBASE_VERSION_MAJOR 2
#define ILMBASE_VERSION_MINOR 4
#define ILMBASE_VERSION_PATCH 1

#define ILMBASE_VERSION_HEX ((uint32_t(ILMBASE_VERSION_MAJOR) << 24) | \
                             (uint32_t(ILMBASE_VERSION_MINOR) << 16) | \
                             (uint32_t(ILMBASE_VERSION_PATCH) <<  8))

#endif // INCLUDED_ILMBASE_CONFIG_H
