/*
 * gazette_version.h - the version, in one place.
 *
 * Three things have to agree and cannot see each other: the About window,
 * the 'vers' resource Rez compiles for the Finder, and the User-Agent the
 * fetcher sends. Rez cannot read this header, so Resources/Gazette.r still
 * carries the number literally -- but it says so, and it names this file.
 */
#ifndef GAZETTE_VERSION_H
#define GAZETTE_VERSION_H

#define GAZETTE_VERSION_MAJOR  0
#define GAZETTE_VERSION_MINOR  1
#define GAZETTE_VERSION_PATCH  0

#define GAZETTE_VERSION_STRING "0.1.0"

#endif /* GAZETTE_VERSION_H */
