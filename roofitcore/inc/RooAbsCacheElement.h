/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 *    File: $Id$
 * Authors:                                                                  *
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu       *
 *   DK, David Kirkby,    UC Irvine,         dkirkby@uci.edu                 *
 *                                                                           *
 * Copyright (c) 2000-2005, Regents of the University of California          *
 *                          and Stanford University. All rights reserved.    *
 *                                                                           *
 * Redistribution and use in source and binary forms,                        *
 * with or without modification, are permitted according to the terms        *
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)             *
 *****************************************************************************/
#ifndef ROO_ABS_CACHE_ELEMENT
#define ROO_ABS_CACHE_ELEMENT

#include "Rtypes.h"
#include "RooAbsArg.h"

#include <iostream> 

class RooAbsCollection ;
class RooArgSet ;
class RooArgList ;

class RooAbsCacheElement {

public:

  virtual Bool_t redirectServersHook(const RooAbsCollection& /*newServerList*/, Bool_t /*mustReplaceAll*/, Bool_t /*nameChange*/, Bool_t /*isRecursive*/)  ;
  virtual void printCompactTreeHook(std::ostream&, const char *, Int_t curElem, Int_t totElem) ;

  enum Action { OperModeChange,OptimizeCaching,FindConstantNodes } ;
  virtual RooArgList containedArgs(Action) = 0 ;
  virtual void operModeHook(RooAbsArg::OperMode) ;
  virtual void optimizeCacheMode(const RooArgSet& obs, RooArgSet& optNodes, RooLinkedList& processedNodes)  ;
  virtual void findConstantNodes(const RooArgSet& obs, RooArgSet& cacheList, RooLinkedList& processedNodes) ;
   
protected:

  ClassDef(RooAbsCacheElement,1) // Base class for cache elements

} ;


#endif 
