/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 *    File: $Id: RooAbsGoodnessOfFit.cc,v 1.6 2003/01/14 00:07:42 wverkerke Exp $
 * Authors:                                                                  *
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu       *
 *   DK, David Kirkby,    UC Irvine,         dkirkby@uci.edu                 *
 *                                                                           *
 * Copyright (c) 2000-2002, Regents of the University of California          *
 *                          and Stanford University. All rights reserved.    *
 *                                                                           *
 * Redistribution and use in source and binary forms,                        *
 * with or without modification, are permitted according to the terms        *
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)             *
 *****************************************************************************/

// -- CLASS DESCRIPTION [PDF] --
// RooAbsGoodnessOfFit is the abstract base class for goodness-of-fit
// for all goodness-of-fit variables. Goodness-of-fit quantities that
// evaluate the PDF at each data point should inherit from the RooAbsOptGoodnessOfFit
// class which implements several generic optimizations that can be done for
// such quantities.
//
// This base class organizes calculation of GOF values for RooSimultaneous PDF
// as a sum of GOF-values for the PDF components of the simultaneous PDF
// (more efficient) and organizes parallel calculation of GOF values.
// For the latter, the GOF values is calculated in partitions in parallel executing
// processes and later combined in the main thread.


#include "RooFitCore/RooAbsGoodnessOfFit.hh"
#include "RooFitCore/RooAbsPdf.hh"
#include "RooFitCore/RooSimultaneous.hh"
#include "RooFitCore/RooAbsData.hh"
#include "RooFitCore/RooArgSet.hh"
#include "RooFitCore/RooRealVar.hh"
#include "RooFitCore/RooNLLVar.hh"
#include "RooFitCore/RooRealMPFE.hh"
#include "RooFitCore/RooErrorHandler.hh"

ClassImp(RooAbsGoodnessOfFit)
;

RooAbsGoodnessOfFit::RooAbsGoodnessOfFit(const char *name, const char *title, RooAbsPdf& pdf, RooAbsData& data,
					 const RooArgSet& projDeps, Int_t nCPU) : 
  RooAbsReal(name,title),
  _paramSet("paramSet","Set of parameters",this),
  _pdf(&pdf),
  _data(&data),
  _projDeps((RooArgSet*)projDeps.Clone()),
  _simCount(1),
  _nGof(0),
  _gofArray(0),
  _nCPU(nCPU),
  _mpfeArray(0)
{
  // Register all parameters as servers 
  RooArgSet* params = pdf.getParameters(&data) ;
  _paramSet.add(*params) ;
  delete params ;

  if (nCPU>1) {

    _gofOpMode = MPMaster ;

  } else {

    // Determine if PDF is a RooSimultaneous
    Bool_t simMode = dynamic_cast<RooSimultaneous*>(&pdf)?kTRUE:kFALSE ;
    
    if (simMode) {
      _gofOpMode = SimMaster ;
    } else {
      _gofOpMode = Slave ;
    }
  }

  _setNum = 0 ;
  _numSets = 1 ;
  _init = kFALSE ;
  _nEvents = data.numEntries() ;
}



RooAbsGoodnessOfFit::RooAbsGoodnessOfFit(const RooAbsGoodnessOfFit& other, const char* name) : 
  RooAbsReal(other,name), 
  _paramSet("paramSet",this,other._paramSet),
  _pdf(other._pdf),
  _data(other._data),
  _projDeps((RooArgSet*)other._projDeps->Clone()),
  _simCount(other._simCount),
  _init(other._init),
  _gofOpMode(other._gofOpMode),
  _nEvents(other._nEvents),
  _setNum(other._setNum),
  _numSets(other._numSets),
  _nCPU(other._nCPU)
{
  if (operMode()==SimMaster) {
    _nGof = other._nGof ; 
    _gofArray = new pRooAbsGoodnessOfFit[_nGof] ;
    Int_t i ;
    for (i=0 ; i<_nGof ; i++) {
      _gofArray[i] = (RooAbsGoodnessOfFit*) other._gofArray[i]->Clone() ;
    }
  }

  if (operMode()==MPMaster) {
    _nCPU = other._nCPU ; 
    _mpfeArray = new pRooRealMPFE[_nCPU] ;
    Int_t i ;
    for (i=0 ; i<_nCPU ; i++) {
      _mpfeArray[i] = (RooRealMPFE*) other._mpfeArray[i]->Clone() ;
    }
  }

  
}



RooAbsGoodnessOfFit::~RooAbsGoodnessOfFit()
{
  if (_gofOpMode==MPMaster && _init) {
    Int_t i ;
    for (i=0 ; i<_nCPU ; i++) {
      delete _mpfeArray[i] ;
    }
    delete[] _mpfeArray ;
  }

  if (_gofOpMode==SimMaster && _init) {
    Int_t i ;
    for (i=0 ; i<_nGof ; i++) {
      delete _gofArray[i] ;      
    }
    delete[] _gofArray ;
  }

  delete _projDeps ;
}



Double_t RooAbsGoodnessOfFit::evaluate() const
{
  // One-time Initialization
  if (!_init) {
    const_cast<RooAbsGoodnessOfFit*>(this)->initialize() ;
  }

  if (_gofOpMode==SimMaster) {

    // Evaluate array of owned GOF objects
    Double_t ret = combinedValue((RooAbsReal**)_gofArray,_nGof) ;
    return ret ;

  } else if (_gofOpMode==MPMaster) {

    // Start calculations in parallel 
    Int_t i ;
    for (i=0 ; i<_nCPU ; i++) {
      _mpfeArray[i]->calculate() ;
    }
    Double_t ret = combinedValue((RooAbsReal**)_mpfeArray,_nCPU) ;
    return ret ;

  } else {

    // Evaluate as straight PDF
    Int_t nFirst = _nEvents * _setNum / _numSets ;
    Int_t nLast = _nEvents * (_setNum+1) / _numSets ;
    Double_t ret =  evaluatePartition(nFirst,nLast) ;
    return ret ;

  }
}



Bool_t RooAbsGoodnessOfFit::initialize() 
{
  if (_init) return kFALSE ;

  if (_gofOpMode==MPMaster) {
    initMPMode(_pdf,_data,_projDeps) ;
  } else if (_gofOpMode==SimMaster) {
    initSimMode((RooSimultaneous*)_pdf,_data,_projDeps) ;
  }
  _init = kTRUE ;
  return kFALSE ;
}



Bool_t RooAbsGoodnessOfFit::redirectServersHook(const RooAbsCollection& newServerList, Bool_t mustReplaceAll, Bool_t nameChange, Bool_t isRecursive) 
{
  if (_gofOpMode==SimMaster) {
    // Forward to slaves
    Int_t i ;
    for (i=0 ; i<_nGof ; i++) {
      if (_gofArray[i]) {
	_gofArray[i]->recursiveRedirectServers(newServerList,mustReplaceAll,nameChange) ;
      }
    }
  } else if (_gofOpMode==MPMaster) {
    // WVE implement this
  }
  return kFALSE ;
}


void RooAbsGoodnessOfFit::constOptimize(ConstOpCode opcode) 
{
  Int_t i ;
  initialize() ;
  if (_gofOpMode==SimMaster) {
    // Forward to slaves    
    for (i=0 ; i<_nGof ; i++) {
      if (_gofArray[i]) _gofArray[i]->constOptimize(opcode) ;
    }
  } else if (_gofOpMode==MPMaster) {
    for (i=0 ; i<_nCPU ; i++) {
      _mpfeArray[i]->constOptimize(opcode) ;
    }
  }
}



void RooAbsGoodnessOfFit::setMPSet(Int_t setNum, Int_t numSets) 
{
  _setNum = setNum ; _numSets = numSets ;
  if (_gofOpMode==SimMaster) {
    // Forward to slaves
    initialize() ;
    Int_t i ;
    for (i=0 ; i<_nGof ; i++) {
      if (_gofArray[i]) _gofArray[i]->setMPSet(setNum,numSets) ;
    }
  } 
}


void RooAbsGoodnessOfFit::initMPMode(RooAbsPdf* pdf, RooAbsData* data, const RooArgSet* projDeps)
{
  Int_t i ;
  _mpfeArray = new pRooRealMPFE[_nCPU] ;

  // Create proto-goodness-of-fit 
  //cout << "initMPMode -- creating prototype gof" << endl ;
  RooAbsGoodnessOfFit* gof = create(GetName(),GetTitle(),*pdf,*data,*projDeps) ;

  for (i=0 ; i<_nCPU ; i++) {

    //cout << "initMPMode -- tayloring prototype for partition " << i << endl ;
    gof->setMPSet(i,_nCPU) ;
    gof->SetName(Form("%s_GOF%d",GetName(),i)) ;
    gof->SetTitle(Form("%s_GOF%d",GetTitle(),i)) ;
    
    Bool_t doInline = (i==_nCPU-1) ;
    if (!doInline) cout << "RooAbsGoodnessOfFit::initMPMode: starting remote GOF server process #" << i << endl ; 
    //cout << "initMPMode -- creating MP front-end" << endl ;
    _mpfeArray[i] = new RooRealMPFE(Form("%s_MPFE%d",GetName(),i),Form("%s_MPFE%d",GetTitle(),i),*gof,doInline) ;    
    //cout << "initMPMode -- initializing MP front-end" << endl ;
    _mpfeArray[i]->initialize() ;
  }
  //cout << "initMPMode --- done" << endl ;
  return ;
}


void RooAbsGoodnessOfFit::initSimMode(RooSimultaneous* simpdf, RooAbsData* data, const RooArgSet* projDeps)
{
  RooAbsCategoryLValue& simCat = (RooAbsCategoryLValue&) simpdf->indexCat() ;


  TString simCatName(simCat.GetName()) ;
  TList* dsetList = const_cast<RooAbsData*>(data)->split(simCat) ;
  if (!dsetList) {
    cout << "RooAbsGoodnessOfFit::initSimMode(" << GetName() << ") unable to split dataset, abort" << endl ;
    RooErrorHandler::softAbort() ;
  }

  // Count number of used states
  Int_t n(0) ;
  _nGof = 0 ;
  RooCatType* type ;
  TIterator* catIter = simCat.typeIterator() ;
  while(type=(RooCatType*)catIter->Next()){

    // Retrieve the PDF for this simCat state
    RooAbsPdf* pdf =  simpdf->getPdf(type->GetName()) ;
    RooAbsData* dset = (RooAbsData*) dsetList->FindObject(type->GetName()) ;

    if (pdf && dset) {      
      _nGof++ ;
    }
  }

  // Allocate arrays 
  _gofArray = new pRooAbsGoodnessOfFit[_nGof] ;

  // Create array of regular fit contexts, containing subset of data and single fitCat PDF
  catIter->Reset() ;
  while(type=(RooCatType*)catIter->Next()){

    // Retrieve the PDF for this simCat state
    RooAbsPdf* pdf =  simpdf->getPdf(type->GetName()) ;
    RooAbsData* dset = (RooAbsData*) dsetList->FindObject(type->GetName()) ;

    if (pdf && dset) {      
      cout << "RooAbsGoodnessOfFit::initSimMode: creating slave GOF calculator #" << n << " for state " << type->GetName() 
	   << " (" << dset->numEntries() << " dataset entries)" << endl ;
      _gofArray[n] = create(type->GetName(),type->GetName(),*pdf,*dset,*projDeps) ;
      _gofArray[n]->setSimCount(_nGof) ;
      
      // Servers may have been redirected between instantiation and (deferred) initialization
      _gofArray[n]->recursiveRedirectServers(_paramSet) ;
      n++ ;
    } else {
      if (!dset && pdf) {
	cout << "RooAbsGoodnessOfFit::initSimMode: state " << type->GetName() 
	     << " has no data entries, no slave GOF calculator created" << endl ;
      }      
    }
  }

  dsetList->Delete() ;
  delete dsetList ;
  delete catIter ;
}





