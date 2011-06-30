/* libvisio
 * Copyright (C) 2011 Fridrich Strba <fridrich.strba@bluewin.ch>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02111-1301 USA
 */

#include <libwpd-stream/libwpd-stream.h>
#include <locale.h>
#include <sstream>
#include <string>
#include <cmath>
#include <set>
#include "libvisio_utils.h"
#include "VSDXParser.h"
#include "VSDInternalStream.h"
#include "VSDXDocumentStructure.h"
#include "VSDXCollector.h"

libvisio::VSDXParser::VSDXParser(WPXInputStream *input, libwpg::WPGPaintInterface *painter)
  : m_input(input), m_painter(painter), m_header(), m_collector(0)
{}

libvisio::VSDXParser::~VSDXParser()
{}


bool libvisio::VSDXParser::parseDocument(WPXInputStream *input)
{
  const unsigned int SHIFT = 4;

  unsigned int ptrType;
  unsigned int ptrOffset;
  unsigned int ptrLength;
  unsigned int ptrFormat;

  // Parse out pointers to other streams from trailer
  input->seek(SHIFT, WPX_SEEK_SET);
  unsigned offset = readU32(input);
  input->seek(offset+SHIFT, WPX_SEEK_SET);
  unsigned int pointerCount = readU32(input);
  input->seek(SHIFT, WPX_SEEK_CUR);
  for (unsigned int i = 0; i < pointerCount; i++)
  {
    ptrType = readU32(input);
    input->seek(4, WPX_SEEK_CUR); // Skip dword
    ptrOffset = readU32(input);
    ptrLength = readU32(input);
    ptrFormat = readU16(input);

    bool compressed = ((ptrFormat & 2) == 2);
    m_input->seek(ptrOffset, WPX_SEEK_SET);
    WPXInputStream *tmpInput = new VSDInternalStream(m_input, ptrLength, compressed);

    switch (ptrType)
    {
    case VSD_PAGE:
      handlePage(tmpInput);
      break;
    case VSD_PAGES:
      handlePages(tmpInput);
      break;
    case VSD_COLORS:
      readColours(tmpInput);
      break;
    default:
      break;
    }

    delete tmpInput;
  }

  return true;
}

void libvisio::VSDXParser::handlePages(WPXInputStream *input)
{
  unsigned int ptrType;
  unsigned int ptrOffset;
  unsigned int ptrLength;
  unsigned int ptrFormat;

  unsigned int offset = readU32(input);
  input->seek(offset, WPX_SEEK_SET);
  unsigned int pointerCount = readU32(input);
  input->seek(4, WPX_SEEK_CUR); // Ignore 0x0 dword

  for (unsigned int i = 0; i < pointerCount; i++)
  {
    ptrType = readU32(input);
    input->seek(4, WPX_SEEK_CUR); // Skip dword
    ptrOffset = readU32(input);
    ptrLength = readU32(input);
    ptrFormat = readU16(input);

    bool compressed = ((ptrFormat & 2) == 2);
    m_input->seek(ptrOffset, WPX_SEEK_SET);
    WPXInputStream *tmpInput = new VSDInternalStream(m_input, ptrLength, compressed);

    switch (ptrType)
    {
    case VSD_PAGE:
      handlePage(tmpInput);
      break;
    case VSD_PAGES:
      handlePages(tmpInput);
      break;
    case VSD_COLORS:
      readColours(tmpInput);
      break;
    default:
      break;
    }

    delete tmpInput;
  }
}

void libvisio::VSDXParser::handlePage(WPXInputStream *input)
{
  long endPos = 0;

  m_collector->startPage();

  while (!input->atEOS())
  {
    if (!getChunkHeader(input))
      break;
    endPos = m_header.dataLength+m_header.trailer+input->tell();

    VSD_DEBUG_MSG(("Shape: parsing chunk type %x\n", m_header.chunkType));
    switch (m_header.chunkType)
    {
    case VSD_SHAPE_GROUP:
    case VSD_SHAPE_SHAPE:
    case VSD_SHAPE_FOREIGN:
      readShape(input);
      break;
    case VSD_XFORM_DATA:
      readXFormData(input);
      break;
    case VSD_SHAPE_LIST:
      readShapeList(input);
      break;
    case VSD_SHAPE_ID:
      readShapeID(input);
      break;
    case VSD_LINE:
      readLine(input);
      break;
    case VSD_FILL_AND_SHADOW:
      readFillAndShadow(input);
      break;
    case VSD_GEOM_LIST:
      readGeomList(input);
      break;
    case VSD_GEOMETRY:
      readGeometry(input);
      break;
    case VSD_MOVE_TO:
      readMoveTo(input);
      break;
    case VSD_LINE_TO:
      readLineTo(input);
      break;
    case VSD_ARC_TO:
      readArcTo(input);
      break;
    case VSD_ELLIPSE:
      readEllipse(input);
      break;
    case VSD_ELLIPTICAL_ARC_TO:
      readEllipticalArcTo(input);
      break;
    case VSD_FOREIGN_DATA_TYPE:
      readForeignDataType(input);
      break;
    case VSD_FOREIGN_DATA:
      readForeignData(input);
      break;
    case VSD_PAGE_PROPS:
      readPageProps(input);
    default:
      m_collector->collectUnhandledChunk(m_header.id, m_header.level);
    }

    input->seek(endPos, WPX_SEEK_SET);
  }
  m_geomList.handle(m_collector);
  m_geomList.clear();
  m_collector->endPage();
}

// --- READERS ---

void libvisio::VSDXParser::readEllipticalArcTo(WPXInputStream *input)
{
  input->seek(1, WPX_SEEK_CUR);
  double x3 = readDouble(input); // End x
  input->seek(1, WPX_SEEK_CUR);
  double y3 = readDouble(input); // End y
  input->seek(1, WPX_SEEK_CUR);
  double x2 = readDouble(input); // Mid x
  input->seek(1, WPX_SEEK_CUR);
  double y2 = readDouble(input); // Mid y
  input->seek(1, WPX_SEEK_CUR);
  double angle = readDouble(input); // Angle
  input->seek(1, WPX_SEEK_CUR);
  double ecc = readDouble(input); // Eccentricity

  m_geomList.addEllipticalArcTo(m_header.id, m_header.level, x3, y3, x2, y2, angle, ecc);
}


void libvisio::VSDXParser::readForeignData(WPXInputStream *input)
{
  unsigned long tmpBytesRead = 0;
  const unsigned char *buffer = input->read(m_header.dataLength, tmpBytesRead);
  if (m_header.dataLength != tmpBytesRead)
    return;
  WPXBinaryData binaryData(buffer, tmpBytesRead);

  m_collector->collectForeignData(m_header.id, m_header.level, binaryData);
}

void libvisio::VSDXParser::readEllipse(WPXInputStream *input)
{
  input->seek(1, WPX_SEEK_CUR);
  double cx = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double cy = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double aa = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  /* double bb = */ readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  /* double cc = */ readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double dd = readDouble(input);

  m_geomList.addEllipse(m_header.id, m_header.level, cx, cy, aa, dd);
}

void libvisio::VSDXParser::readLine(WPXInputStream *input)
{
  input->seek(1, WPX_SEEK_CUR);
  double strokeWidth = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  Colour c;
  c.r = readU8(input);
  c.g = readU8(input);
  c.b = readU8(input);
  c.a = readU8(input);
  unsigned linePattern = readU8(input);

  m_collector->collectLine(m_header.id, m_header.level, strokeWidth, c, linePattern);
}

void libvisio::VSDXParser::readFillAndShadow(WPXInputStream *input)
{
  unsigned int colourIndexFG = readU8(input);
  input->seek(4, WPX_SEEK_CUR);
  unsigned int colourIndexBG = readU8(input);
  input->seek(4, WPX_SEEK_CUR);
  unsigned fillPattern = readU8(input);

  m_collector->collectFillAndShadow(m_header.id, m_header.level, colourIndexFG, colourIndexBG, fillPattern);
}


void libvisio::VSDXParser::readGeomList(WPXInputStream *input)
{
  uint32_t subHeaderLength = readU32(input);
  uint32_t childrenListLength = readU32(input);
  input->seek(subHeaderLength, WPX_SEEK_CUR);
  std::vector<unsigned> geometryOrder;
  geometryOrder.reserve(childrenListLength / sizeof(uint32_t));
  for (unsigned i = 0; i < (childrenListLength / sizeof(uint32_t)); i++)
    geometryOrder.push_back(readU32(input));

  m_geomList.setElementsOrder(geometryOrder);
  // We want the collectors to still get the level information
  m_collector->collectGeomList(m_header.id, m_header.level);
}

void libvisio::VSDXParser::readGeometry(WPXInputStream *input)
{
  unsigned geomFlags = readU8(input);

  m_geomList.addGeometry(m_header.id, m_header.level, geomFlags);
}

void libvisio::VSDXParser::readMoveTo(WPXInputStream *input)
{
  input->seek(1, WPX_SEEK_CUR);
  double x = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double y = readDouble(input);

  m_geomList.addMoveTo(m_header.id, m_header.level, x, y);
}

void libvisio::VSDXParser::readLineTo(WPXInputStream *input)
{
  input->seek(1, WPX_SEEK_CUR);
  double x = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double y = readDouble(input);

  m_geomList.addLineTo(m_header.id, m_header.level, x, y);
}

void libvisio::VSDXParser::readArcTo(WPXInputStream *input)
{
  input->seek(1, WPX_SEEK_CUR);
  double x2 = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double y2 = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double bow = readDouble(input);

  m_geomList.addArcTo(m_header.id, m_header.level, x2, y2, bow);
}

void libvisio::VSDXParser::readXFormData(WPXInputStream *input)
{
  XForm xform;
  input->seek(1, WPX_SEEK_CUR);
  xform.pinX = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.pinY = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.width = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.height = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.pinLocX = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.pinLocY = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.angle = readDouble(input);
  xform.flipX = (readU8(input) != 0);
  xform.flipY = (readU8(input) != 0);

  m_collector->collectXFormData(m_header.id, m_header.level, xform);
}

void libvisio::VSDXParser::readShapeID(WPXInputStream *input)
{
  unsigned shapeId = readU32(input);

  m_collector->collectShapeID(m_header.id, m_header.level, shapeId);
}

void libvisio::VSDXParser::readShapeList(WPXInputStream *input)
{
  uint32_t subHeaderLength = readU32(input);
  uint32_t childrenListLength = readU32(input);
  input->seek(subHeaderLength, WPX_SEEK_CUR);
  std::vector<unsigned> shapeList;
  shapeList.reserve(childrenListLength / sizeof(uint32_t));
  for (unsigned i = 0; i < (childrenListLength / sizeof(uint32_t)); i++)
    shapeList.push_back(readU32(input));

  m_collector->collectShapeList(m_header.id, m_header.level, shapeList);
}

void libvisio::VSDXParser::readForeignDataType(WPXInputStream *input)
{
  input->seek(0x24, WPX_SEEK_CUR);
  unsigned foreignType = readU16(input);
  input->seek(0xb, WPX_SEEK_CUR);
  unsigned foreignFormat = readU32(input);

  VSD_DEBUG_MSG(("Found foreign data, type %d format %d\n", foreignType, foreignFormat));

  m_collector->collectForeignDataType(m_header.id, m_header.level, foreignType, foreignFormat);
}

void libvisio::VSDXParser::readPageProps(WPXInputStream *input)
{
  // Skip bytes representing unit to *display* (value is always inches)
  input->seek(1, WPX_SEEK_CUR);
  double pageWidth = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  double pageHeight = readDouble(input);
  input->seek(19, WPX_SEEK_CUR);
  /* m_scale = */ readDouble(input);

  m_collector->collectPageProps(m_header.id, m_header.level, pageWidth, pageHeight);
}

void libvisio::VSDXParser::readShape(WPXInputStream * /* input */)
{
  m_collector->collectShape(m_header.id, m_header.level);
}

void libvisio::VSDXParser::readColours(WPXInputStream *input)
{
  input->seek(6, WPX_SEEK_SET);
  unsigned int numColours = readU8(input);
  Colour tmpColour;

  input->seek(1, WPX_SEEK_CUR);

  std::vector<Colour> colours;

  for (unsigned int i = 0; i < numColours; i++)
  {
    tmpColour.r = readU8(input);
    tmpColour.g = readU8(input);
    tmpColour.b = readU8(input);
    tmpColour.a = readU8(input);

    colours.push_back(tmpColour);
  }
  m_collector->collectColours(colours);
}

