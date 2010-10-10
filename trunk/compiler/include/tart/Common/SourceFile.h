/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#ifndef TART_COMMON_SOURCEFILE_H
#define TART_COMMON_SOURCEFILE_H

#ifndef TART_COMMON_GC_H
#include "tart/Common/GC.h"
#endif

#ifndef TART_COMMON_SOURCEREGION_H
#include "tart/Common/SourceRegion.h"
#endif

//#include <llvm/Support/DataTypes.h>
#include <llvm/System/Path.h>
#include <string>
#include <fstream>
#include <list>
#include <vector>
#include <sstream>

namespace tart {

// -------------------------------------------------------------------
// Abstract interface representing the source of program text.
class ProgramSource : public SourceRegion {
public:
  ProgramSource(const std::string & path)
    : filePath(path)
    , lineOffsets()
  {
    lineOffsets.push_back(0);
  }

  virtual ~ProgramSource() {}

  /** Return the path of this file. */
  const std::string & getFilePath() const { return filePath; }

  /** Opens the file and returns an input stream. */
  virtual std::istream & open() = 0;

  /** Closes the input stream. */
  virtual void close() = 0;

  /** Read a segment from the stream (for error reporting) */
  virtual bool readLineAt(uint32_t start, std::string & result) = 0;

  /** Returns true if the stream is good for reading. */
  virtual bool isValid() = 0;

  /** Mark a line break at the specified offset */
  void newLine(uint32_t offset) {
      lineOffsets.push_back(offset);
  }

  /** Return the pointer to the program source. Overloaded for testing
      purposes.
  */
  virtual ProgramSource * get() { return this; }

  /** Calculate the token position for the given source location. */
  TokenPosition tokenPosition(const SourceLocation & loc);

  // Overrides

  RegionType regionType() const { return FILE; }
  SourceRegion * parentRegion() const { return NULL; }
  void trace() const {}

  // Casting

  static inline bool classof(const ProgramSource *) { return true; }
  static inline bool classof(const SourceRegion * ss) {
    return ss->regionType() == FILE;
  }

protected:
  std::string filePath;       // Path to the file
  std::vector<uint32_t> lineOffsets;  // The start offset of each line
};

// -------------------------------------------------------------------
// A source file.
class SourceFile : public ProgramSource {
private:
  std::ifstream stream;

public:
  SourceFile(const std::string & path)
    : ProgramSource(path)
  {
  }

  std::istream & open();
  void close();
  bool readLineAt(uint32_t start, std::string & result);
  bool isValid() { return stream.good(); }
};

/// -------------------------------------------------------------------
/// Program source in a string.
class SourceString : public ProgramSource {
private:
  std::istringstream  stream;

public:
  SourceString(const char * src)
    : tart::ProgramSource("")
    , stream(src)
  {
  }

  std::istream & open() { return stream; }
  void close() {};
  bool readLineAt(uint32_t lineIndex, std::string & result);
  bool isValid() { return true; }
};

// -------------------------------------------------------------------
// Get the token position for a given source location.
static TokenPosition tokenPosition(const SourceLocation & loc) {
  if (loc.region) {
    return loc.region->tokenPosition(loc);
  } else {
    return TokenPosition();
  }
}

}
#endif
