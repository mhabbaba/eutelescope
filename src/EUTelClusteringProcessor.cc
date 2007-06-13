// -*- mode: c++; mode: auto-fill; mode: flyspell-prog; -*-
// Author Antonio Bulgheroni, INFN <mailto:antonio.bulgheroni@gmail.com>
// Version $Id: EUTelClusteringProcessor.cc,v 1.13 2007-06-13 11:45:43 bulgheroni Exp $
/*
 *   This source code is part of the Eutelescope package of Marlin.
 *   You are free to use this source files for your own development as
 *   long as it stays in a public research context. You are not
 *   allowed to use it for commercial purpose. You must put this
 *   header with author names in all development based on this file.
 *
 */

// eutelescope includes ".h" 
#include "EUTELESCOPE.h"
#include "EUTelExceptions.h"
#include "EUTelRunHeaderImpl.h"
#include "EUTelEventImpl.h"
#include "EUTelClusteringProcessor.h"
#include "EUTelVirtualCluster.h"
#include "EUTelFFClusterImpl.h"
#include "EUTelExceptions.h"

// marlin includes ".h"
#include "marlin/Processor.h"
#include "marlin/AIDAProcessor.h"
#include "marlin/Exceptions.h"

// lcio includes <.h> 
#include <UTIL/CellIDEncoder.h>
#include <IMPL/TrackerRawDataImpl.h>
#include <IMPL/TrackerDataImpl.h>
#include <IMPL/TrackerPulseImpl.h>
#include <IMPL/LCCollectionVec.h>

#ifdef MARLIN_USE_AIDA
// aida includes <.h>
#include <AIDA/IHistogramFactory.h>
#include <AIDA/IHistogram1D.h>
#include <AIDA/IHistogram2D.h>
#include <AIDA/ITree.h>
#endif

// system includes <>
#ifdef MARLINDEBUG
#include <fstream> 
#endif
#include <string>
#include <sstream>
#include <vector>

using namespace std;
using namespace lcio;
using namespace marlin;
using namespace eutelescope;

#ifdef MARLINDEBUG
ofstream logfile;
#endif

// definition of static members mainly used to name histograms
#ifdef MARLIN_USE_AIDA
std::string EUTelClusteringProcessor::_clusterSignalHistoName   = "clusterSignal";
std::string EUTelClusteringProcessor::_seedSignalHistoName      = "seedSignal";
std::string EUTelClusteringProcessor::_hitMapHistoName          = "hitMap";
#endif

EUTelClusteringProcessor::EUTelClusteringProcessor () :Processor("EUTelClusteringProcessor") {

  // modify processor description
  _description =
    "EUTelClusteringProcessor subtract the pedestal value from the input data";

  // first of all we need to register the input collection
  registerInputCollection (LCIO::TRACKERDATA, "DataCollectionName",
			   "Input calibrated data collection name",
			   _dataCollectionName, string ("data"));

  registerInputCollection (LCIO::TRACKERDATA, "NoiseCollectionName",
			   "Noise (input) collection name",
			   _noiseCollectionName, string("noise"));

  registerInputCollection (LCIO::TRACKERRAWDATA, "StatusCollectionName",
			   "Pixel status (input) collection name",
			   _statusCollectionName, string("status"));

  registerOutputCollection(LCIO::TRACKERPULSE, "PulseCollectionName",
			   "Cluster (output) collection name",
			   _pulseCollectionName, string("cluster"));

  // I believe it is safer not allowing the dummyCollection to be
  // renamed by the user. I prefer to set it once for ever here and
  // eventually, only if really needed, in the future allow add
  // another registerOutputCollection.
  _dummyCollectionName = "original_data";


  // now the optional parameters
  registerProcessorParameter ("ClusteringAlgo",
			      "Select here which algorithm should be used for clustering",
			      _clusteringAlgo, string(EUTELESCOPE::FIXEDFRAME));
  
  registerProcessorParameter ("ClusterSizeX",
			      "Maximum allowed cluster size along x (only odd numbers)",
			      _xClusterSize, static_cast<int> (5));

  registerProcessorParameter ("ClusterSizeY",
			      "Maximum allowed cluster size along y (only odd numbers)",
			      _yClusterSize, static_cast<int> (5));

  registerProcessorParameter ("SeedPixelCut",
			      "Threshold in SNR for seed pixel identification",
			      _seedPixelCut, static_cast<float> (4.5));

  registerProcessorParameter ("ClusterCut",
			      "Threshold in SNR for cluster identification",
			      _clusterCut, static_cast<float> (3.0));

#ifdef MARLIN_USE_AIDA
  IntVec clusterNxNExample;
  clusterNxNExample.push_back(3);
  clusterNxNExample.push_back(5);
  
  registerOptionalParameter("ClusterNxN", "The list of cluster NxN to be filled."
			    "For example 3 means filling the 3x3 histogram spectrum",
			    _clusterSpectraNxNVector, clusterNxNExample, clusterNxNExample.size());

  IntVec clusterNExample;
  clusterNExample.push_back(4);
  clusterNExample.push_back(9);
  clusterNExample.push_back(14);
  clusterNExample.push_back(19);
  clusterNExample.push_back(25);
  registerOptionalParameter("ClusterN", "The list of cluster N to be filled."
			    "For example 7 means filling the cluster spectra with the 7 most significant pixels",
			    _clusterSpectraNVector, clusterNExample, clusterNExample.size() );
#endif

  registerProcessorParameter("HistogramFilling","Switch on or off the histogram filling",
			     _fillHistos, static_cast< bool > ( true ) );


}


void EUTelClusteringProcessor::init () {
  // this method is called only once even when the rewind is active
  // usually a good idea to
  printParameters ();

  // in the case the FIXEDFRAME algorithm is selected, the check if
  // the _xClusterSize and the _yClusterSize are odd numbers
  if ( _clusteringAlgo == EUTELESCOPE::FIXEDFRAME ) {
    bool isZero = ( _xClusterSize <= 0 );
    bool isEven = ( _xClusterSize % 2 == 0 );
    if ( isZero || isEven ) {
      throw InvalidParameterException("_xClusterSize has to be positive and odd");
    }
    isZero = ( _yClusterSize <= 0 );
    isEven = ( _yClusterSize % 2 == 0 );
    if ( isZero || isEven ) {
      throw InvalidParameterException("_yClusterSize has to be positive and odd");
    }
  }

  // set to zero the run and event counters
  _iRun = 0;
  _iEvt = 0;

  // reset the content of the total cluster vector
  _totCluster.clear();

}

void EUTelClusteringProcessor::processRunHeader (LCRunHeader * rdr) {

  // to make things easier re-cast the input header to the EUTelRunHeaderImpl
  EUTelRunHeaderImpl *  runHeader = static_cast<EUTelRunHeaderImpl*>(rdr);

  // the four vectors containing the first and the last pixel
  // along both the directions
  _minX = runHeader->getMinX();
  _maxX = runHeader->getMaxX();
  _minY = runHeader->getMinY();
  _maxY = runHeader->getMaxY();

#ifdef MARLIN_USE_AIDA
  // let me get from the run header all the available parameter
  _noOfDetector = runHeader->getNoOfDetector();

  // book the histograms now
  if ( _fillHistos ) bookHistos();
#endif

  // increment the run counter
  ++_iRun;

}


void EUTelClusteringProcessor::processEvent (LCEvent * event) {

  EUTelEventImpl * evt = static_cast<EUTelEventImpl*> (event);
  if ( evt->getEventType() == kEORE ) {
    message<DEBUG> ( "EORE found: nothing else to do." );
    return;
  } else if ( evt->getEventType() == kUNKNOWN ) {
    message<WARNING> ( log() << "Event number " << evt->getEventNumber() 
		       << " is of unknown type. Continue considering it as a normal Data Event."  );
  }

  if (_iEvt % 10 == 0)  message<MESSAGE> ( log() <<  "Clustering event " << _iEvt ) ;
  
  if ( _clusteringAlgo == EUTELESCOPE::FIXEDFRAME ) fixedFrameClustering(evt);
  
#ifdef MARLIN_USE_AIDA
  if ( _fillHistos ) fillHistos(event);
#endif

}

void EUTelClusteringProcessor::fixedFrameClustering(LCEvent * evt) {
  
  LCCollectionVec * inputCollectionVec    = dynamic_cast < LCCollectionVec * > (evt->getCollection(_dataCollectionName));
  LCCollectionVec * noiseCollectionVec    = dynamic_cast < LCCollectionVec * > (evt->getCollection(_noiseCollectionName));
  LCCollectionVec * statusCollectionVec   = dynamic_cast < LCCollectionVec * > (evt->getCollection(_statusCollectionName));
  
  if (isFirstEvent()) {
    
#ifdef MARLINDEBUG
    logfile.open("clustering.log");
#endif
    
    // this is the right place to cross check whether the pedestal and
    // the input data are at least compatible. I mean the same number
    // of detectors and the same number of pixels in each place.
    
    if  ( inputCollectionVec->getNumberOfElements() != noiseCollectionVec->getNumberOfElements()) {
      stringstream ss;
      ss << "Input data and pedestal are incompatible" << endl
	 << "Input collection has    " << inputCollectionVec->getNumberOfElements()    << " detectors," << endl
	 << "Noise collection has    " << noiseCollectionVec->getNumberOfElements() << " detectors," << endl;
      throw IncompatibleDataSetException(ss.str());
    }
    
    for (_iDetector = 0; _iDetector < inputCollectionVec->getNumberOfElements(); _iDetector++) 
      _totCluster.push_back(0);
    
    _isFirstEvent = false;
  }
  
#ifdef MARLINDEBUG
  message<DEBUG> ( logfile << "Event " << _iEvt );
#endif
  message<DEBUG> ( log()   << "Event " << _iEvt );

  LCCollectionVec * pulseCollection = new LCCollectionVec(LCIO::TRACKERPULSE);
  LCCollectionVec * dummyCollection = new LCCollectionVec(LCIO::TRACKERDATA);

  for (_iDetector = 0; _iDetector < inputCollectionVec->getNumberOfElements(); _iDetector++) {
    
#ifdef MARLINDEBUG
    message<DEBUG> ( logfile << "  Working on detector " << _iDetector );
#endif
    message<DEBUG> ( log()   << "  Working on detector " << _iDetector );


    // get the calibrated data 
    TrackerDataImpl    * data   = dynamic_cast<TrackerDataImpl*>   (inputCollectionVec->getElementAt(_iDetector));
    TrackerDataImpl    * noise  = dynamic_cast<TrackerDataImpl*>   (noiseCollectionVec->getElementAt(_iDetector));
    TrackerRawDataImpl * status = dynamic_cast<TrackerRawDataImpl*>(statusCollectionVec->getElementAt(_iDetector));

    // reset the status
    resetStatus(status);

    // initialize the cluster counter 
    short clusterCounter = 0;
    short limitExceed = 0;

    _seedCandidateMap.clear();
    
    message<DEBUG> ( log() << "Max signal " << (*max_element(data->getChargeValues().begin(), data->getChargeValues().end()))
		     << "\nMin signal " << (*min_element(data->getChargeValues().begin(), data->getChargeValues().end())) );

    for (unsigned int iPixel = 0; iPixel < data->getChargeValues().size(); iPixel++) {
      if (status->getADCValues()[iPixel] == EUTELESCOPE::GOODPIXEL) {
	if (data->getChargeValues()[iPixel] > _seedPixelCut * noise->getChargeValues()[iPixel]) {
	  _seedCandidateMap.insert(make_pair(data->getChargeValues()[iPixel], iPixel));
	}
      }
    }

    // continue only if seed candidate map is not empty!
    if ( _seedCandidateMap.size() != 0 ) {

#ifdef MARLINDEBUG
      message<DEBUG> ( logfile << "  Seed candidates " << _seedCandidateMap.size() ); 
#endif
      message<DEBUG> ( log()   << "  Seed candidates " << _seedCandidateMap.size() );

      // now built up a cluster for each seed candidate 
      map<float, unsigned int>::iterator mapIter = _seedCandidateMap.end();     
      while ( mapIter != _seedCandidateMap.begin() ) {
	--mapIter;	
	// check if this seed candidate has not been already added to a
	// cluster
	if ( status->adcValues()[(*mapIter).second] == EUTELESCOPE::GOODPIXEL ) {
	  // if we enter here, this means that at least the seed pixel
	  // wasn't added yet to another cluster.  Note that now we need
	  // to build a candidate cluster that has to pass the
	  // clusterCut to be considered a good cluster
	  double clusterCandidateSignal    = 0.;
	  double clusterCandidateNoise2    = 0.;
	  FloatVec clusterCandidateCharges;
	  IntVec   clusterCandidateIndeces;
	  int seedX, seedY;
	  getXYFromIndex((*mapIter).second, seedX, seedY);

	  // start looping around the seed pixel. Remember that the seed
	  // pixel has to stay in the center of cluster
	  ClusterQuality cluQuality = kGoodCluster;
	  for (int yPixel = seedY - (_yClusterSize / 2); yPixel <= seedY + (_yClusterSize / 2); yPixel++) {
	    for (int xPixel =  seedX - (_xClusterSize / 2); xPixel <= seedX + (_xClusterSize / 2); xPixel++) {
	      // always check we are still within the sensor!!!
	      if ( ( xPixel >= _minX[_iDetector] )  &&  ( xPixel <= _maxX[_iDetector] ) &&
		   ( yPixel >= _minY[_iDetector] )  &&  ( yPixel <= _maxY[_iDetector] ) ) {
		int index = getIndexFromXY(xPixel, yPixel);
		bool isHit  = ( status->getADCValues()[index] == EUTELESCOPE::HITPIXEL  );
		bool isGood = ( status->getADCValues()[index] == EUTELESCOPE::GOODPIXEL );
		if ( isGood && !isHit ) {
		  clusterCandidateSignal += data->getChargeValues()[index];
		  clusterCandidateNoise2 += noise->getChargeValues()[index];
		  clusterCandidateCharges.push_back(data->getChargeValues()[index]);
		  clusterCandidateIndeces.push_back(index);
		} else if (isHit) {
		  // this can be a good place to flag the current
		  // cluster as kMergedCluster, but it would introduce
		  // a bias since the at least another cluster (the
		  // one which this pixel belong to) is not flagged.
		  //
		  // In order to flag all merged clusters and possibly
		  // try to separate the different contributions use
		  // the EUTelSeparateClusterProcessor
		  clusterCandidateCharges.push_back(0.);
		} else if (!isGood) {
		  cluQuality = cluQuality | kIncompleteCluster;
		  clusterCandidateCharges.push_back(0.);
		}
	      } else {
		cluQuality = cluQuality | kBorderCluster;
		clusterCandidateCharges.push_back(0.);
	      }
	    }
	  }
	  // at this point we have built the cluster candidate,
	  // we need to validate it
	  if ( clusterCandidateSignal > _clusterCut * sqrt(clusterCandidateNoise2) ) {
	    // the cluster candidate is a good cluster
	    // mark all pixels belonging to the cluster as hit
	    IntVec::iterator indexIter = clusterCandidateIndeces.begin();

	    // the final result of the clustering will enter in a
	    // TrackerPulseImpl in order to be algorithm independent 
	    TrackerPulseImpl * pulse = new TrackerPulseImpl;
	    CellIDEncoder<TrackerPulseImpl> idPulseEncoder(EUTELESCOPE::PULSEDEFAULTENCODING, pulseCollection);
	    idPulseEncoder["sensorID"]      = _iDetector;
	    idPulseEncoder["clusterID"]     = clusterCounter;
	    idPulseEncoder["xSeed"]         = seedX;
	    idPulseEncoder["ySeed"]         = seedY;
	    idPulseEncoder["xCluSize"]      = _xClusterSize;
	    idPulseEncoder["yCluSize"]      = _yClusterSize;
	    idPulseEncoder["type"]          = static_cast<int>(kEUTelFFClusterImpl);
	    idPulseEncoder.setCellID(pulse);	    


	    TrackerDataImpl * cluster = new TrackerDataImpl;
	    CellIDEncoder<TrackerDataImpl> idClusterEncoder(EUTELESCOPE::CLUSTERDEFAULTENCODING, dummyCollection);
	    idClusterEncoder["sensorID"]      = _iDetector;
	    idClusterEncoder["clusterID"]     = clusterCounter;
	    idClusterEncoder["xSeed"]         = seedX;
	    idClusterEncoder["ySeed"]         = seedY;
	    idClusterEncoder["xCluSize"]      = _xClusterSize;
	    idClusterEncoder["yCluSize"]      = _yClusterSize;
	    idClusterEncoder["quality"]       = static_cast<int>(cluQuality);
	    idClusterEncoder.setCellID(cluster);

#ifdef MARLINDEBUG
	    message<DEBUG> ( logfile << "  Cluster no " <<  clusterCounter << " seedX " << seedX << " seedY " << seedY );
#endif	    
	    message<DEBUG> ( log()   << "  Cluster no " <<  clusterCounter << " seedX " << seedX << " seedY " << seedY );

	    
	    while ( indexIter != clusterCandidateIndeces.end() ) {
	      status->adcValues()[(*indexIter)] = EUTELESCOPE::HITPIXEL;
	      ++indexIter;
	    }


	    for (unsigned int iPixel = 0; iPixel < clusterCandidateIndeces.size(); iPixel++) {
#ifdef MARLINDEBUG
	      message<DEBUG> ( logfile << "  x " <<  getXFromIndex(clusterCandidateIndeces[iPixel])
			       << "  y " <<  getYFromIndex(clusterCandidateIndeces[iPixel])
			       << "  s " <<  clusterCandidateCharges[iPixel]);
#endif
	      message<DEBUG> ( log() << "  x " <<  getXFromIndex(clusterCandidateIndeces[iPixel])
			       << "  y " <<  getYFromIndex(clusterCandidateIndeces[iPixel])
			       << "  s " <<  clusterCandidateCharges[iPixel]);
	    }


	    // copy the candidate charges inside the cluster
	    cluster->setChargeValues(clusterCandidateCharges);
	    dummyCollection->push_back(cluster);
	    
	    EUTelFFClusterImpl * eutelCluster = new EUTelFFClusterImpl( cluster );
	    pulse->setCharge(eutelCluster->getTotalCharge());
	    delete eutelCluster;

	    pulse->setQuality(static_cast<int>(cluQuality));
	    pulse->setTrackerData(cluster);
	    pulseCollection->push_back(pulse);

	    // increment the cluster counters
	    _totCluster[_iDetector] += 1;
	    ++clusterCounter;
	    if ( clusterCounter > 256 ) {
	      ++limitExceed;
	      --clusterCounter;
	      message<WARNING> ( log() << "Event " << _iEvt << " contains more than 256 clusters (" 
				 << clusterCounter + limitExceed << ")" );
	    }
	  } else {
	    // the cluster has not passed the cut!

	  }
	}
      }
    }
  }

  ++_iEvt;

  if ( pulseCollection->size() != 0 ) {
    evt->addCollection(pulseCollection,_pulseCollectionName);
    evt->addCollection(dummyCollection,_dummyCollectionName);
  } else {
    delete pulseCollection;
    delete dummyCollection;
    throw SkipEventException(this);
  }
  
}



void EUTelClusteringProcessor::check (LCEvent * evt) {
  // nothing to check here - could be used to fill check plots in reconstruction processor
}
 

void EUTelClusteringProcessor::end() {

  message<MESSAGE> ( "Successfully finished" );

  for (_iDetector = 0; _iDetector < (signed) _totCluster.size() ; _iDetector++) {
#ifdef MARLINDEBUG
    message<DEBUG> ( logfile << "Found " << _totCluster[_iDetector] << " clusters on detector " << _iDetector );
#endif
    message<DEBUG> ( log() << "Found " << _totCluster[_iDetector] << " clusters on detector " << _iDetector );
  }
#ifdef MARLINDEBUG
  logfile.close();
#endif

}


void EUTelClusteringProcessor::resetStatus(IMPL::TrackerRawDataImpl * status) {
  
  ShortVec::iterator iter = status->adcValues().begin();
  while ( iter != status->adcValues().end() ) {
    if ( *iter == EUTELESCOPE::HITPIXEL ) {
      *iter = EUTELESCOPE::GOODPIXEL;
    }
    ++iter; 
  }

}

void EUTelClusteringProcessor::fillHistos (LCEvent * evt) {

  EUTelEventImpl * eutelEvent = static_cast<EUTelEventImpl*> (evt);
  EventType type              = eutelEvent->getEventType();
  
  if ( type == kEORE ) {
    message<DEBUG> ( "EORE found: nothing else to do.");
    return ;
  } else if ( type == kUNKNOWN ) {
    message<WARNING> ( log() << "Event number " << evt->getEventNumber() 
		       << " is of unknown type. Continue considering it as a normal Data Event."  );
  }
  

#ifdef MARLIN_USE_AIDA

  if ( (_iEvt % 10) == 0 ) 
    message<MESSAGE> ( log() << "Filling histogram on event " << _iEvt );
  
  LCCollectionVec * pulseCollectionVec = dynamic_cast<LCCollectionVec*> 
    (evt->getCollection(_pulseCollectionName));
  CellIDDecoder<TrackerPulseImpl> cellDecoder(pulseCollectionVec);

  for ( int iPulse = 0; iPulse < pulseCollectionVec->getNumberOfElements(); iPulse++ ) {
    TrackerPulseImpl * pulse = dynamic_cast<TrackerPulseImpl*> ( pulseCollectionVec->getElementAt(iPulse) );
    ClusterType        type  = static_cast<ClusterType> ( static_cast<int> ( cellDecoder(pulse)["type"] ));
    
    EUTelVirtualCluster * cluster;
    
    if ( type == kEUTelFFClusterImpl ) 
      cluster = new EUTelFFClusterImpl ( static_cast<TrackerDataImpl*> ( pulse->getTrackerData() ) );
    else {
      message<ERROR> ( "Unknown cluster type. Sorry for quitting" );
      throw UnknownDataTypeException("Cluster type unknown");
    }

    int detectorID = cluster->getDetectorID();
    string tempHistoName;

    {
      stringstream ss;
      ss << _clusterSignalHistoName << "-d" << detectorID;
      tempHistoName = ss.str();
    } 
    (dynamic_cast<AIDA::IHistogram1D*> (_aidaHistoMap[tempHistoName]))->fill(cluster->getTotalCharge());
    
    {
      stringstream ss;
      ss << _seedSignalHistoName << "-d" << detectorID;
      tempHistoName = ss.str();
    }
    (dynamic_cast<AIDA::IHistogram1D*> (_aidaHistoMap[tempHistoName]))->fill(cluster->getSeedCharge());

    vector<int >::iterator iter = _clusterSpectraNVector.begin();
    while ( iter != _clusterSpectraNVector.end() ) {
      {
	stringstream ss;
	ss << _clusterSignalHistoName << (*iter) << "-d" << detectorID;
	tempHistoName = ss.str();
      }
      (dynamic_cast<AIDA::IHistogram1D*> (_aidaHistoMap[tempHistoName]))
	->fill(cluster->getClusterCharge((*iter)));
      ++iter;
    }

    iter = _clusterSpectraNxNVector.begin();
    while ( iter != _clusterSpectraNxNVector.end() ) {
      {
	stringstream ss;
	ss << _clusterSignalHistoName << (*iter) << "x" << (*iter) << "-d" << detectorID;
	tempHistoName = ss.str();
      }
      (dynamic_cast<AIDA::IHistogram1D*> (_aidaHistoMap[tempHistoName]))
	->fill(cluster->getClusterCharge((*iter), (*iter)));
      ++iter;
    }

    {
      stringstream ss;
      ss << _hitMapHistoName << "-d" << detectorID;
      tempHistoName = ss.str();
    } 
    int xSeed, ySeed;
    cluster->getSeedCoord(xSeed, ySeed);
    (dynamic_cast<AIDA::IHistogram2D*> (_aidaHistoMap[tempHistoName]))
      ->fill(static_cast<double >(xSeed), static_cast<double >(ySeed), 1.);
    
    delete cluster;
  }

#endif
  
}

void EUTelClusteringProcessor::bookHistos() {

#ifdef MARLIN_USE_AIDA
  // histograms are grouped in loops and detectors
  message<MESSAGE> ( log() << "Booking histograms " );


  string tempHistoName;
  string basePath;
  for (int iDetector = 0; iDetector < _noOfDetector; iDetector++) {
    
    {
      stringstream ss;
      ss << "detector-" << iDetector;
      basePath = ss.str();
    }
    AIDAProcessor::tree(this)->mkdir(basePath.c_str());
    basePath.append("/");

    {
      stringstream ss;
      ss << _clusterSignalHistoName << "-d" << iDetector;
      tempHistoName = ss.str();
    } 

    const int    clusterNBin = 1000;
    const double clusterMin  = 0.;
    const double clusterMax  = 1000.;

    AIDA::IHistogram1D * clusterSignalHisto = 
      AIDAProcessor::histogramFactory(this)->createHistogram1D( (basePath + tempHistoName).c_str(), 
								clusterNBin,clusterMin,clusterMax);
    _aidaHistoMap.insert(make_pair(tempHistoName, clusterSignalHisto));
    clusterSignalHisto->setTitle("Cluster spectrum with all pixels");

    
    vector<int >::iterator iter = _clusterSpectraNVector.begin();
    while ( iter != _clusterSpectraNVector.end() ) {
      {
	stringstream ss;
	ss << _clusterSignalHistoName << (*iter) << "-d" << iDetector;
	tempHistoName = ss.str();
      }
      AIDA::IHistogram1D * clusterSignalNHisto = 
	AIDAProcessor::histogramFactory(this)->createHistogram1D( (basePath + tempHistoName).c_str(),
								  clusterNBin, clusterMin, clusterMax);
      _aidaHistoMap.insert(make_pair(tempHistoName, clusterSignalNHisto) );
      string tempTitle = "Cluster spectrum with the " + (*iter);
      tempTitle.append(" most significant pixels ");
      clusterSignalNHisto->setTitle(tempTitle.c_str());

      ++iter;
    }

    iter = _clusterSpectraNxNVector.begin();
    while ( iter != _clusterSpectraNxNVector.end() ) {
      {
	stringstream ss;
	ss << _clusterSignalHistoName << (*iter) << "x" << (*iter) << "-d" << iDetector;
	tempHistoName = ss.str();
      }
      AIDA::IHistogram1D * clusterSignalNxNHisto = 
	AIDAProcessor::histogramFactory(this)->createHistogram1D( (basePath + tempHistoName).c_str(),
								  clusterNBin, clusterMin, clusterMax);
      _aidaHistoMap.insert(make_pair(tempHistoName, clusterSignalNxNHisto) );
      string tempTitle;
      {
	stringstream ss;
	ss << "Cluster spectrum with " << (*iter) << " by " << (*iter) << " pixels ";
	tempTitle = ss.str();
      }
      clusterSignalNxNHisto->setTitle(tempTitle.c_str());

      ++iter;
    }


    int    seedNBin = 500;
    double seedMin  = 0.;
    double seedMax  = 500.;

    {
      stringstream ss;
      ss << _seedSignalHistoName << "-d" << iDetector;
      tempHistoName = ss.str();
    } 
    AIDA::IHistogram1D * seedSignalHisto =
      AIDAProcessor::histogramFactory(this)->createHistogram1D( (basePath + tempHistoName).c_str(),
								seedNBin, seedMin, seedMax);
    _aidaHistoMap.insert(make_pair(tempHistoName, seedSignalHisto));
    seedSignalHisto->setTitle("Seed pixel spectrum");
  
    
    {
      stringstream ss;
      ss << _hitMapHistoName << "-d" << iDetector;
      tempHistoName = ss.str();
    } 
    int     xBin = _maxX[iDetector] - _minX[iDetector] + 1;
    double  xMin = static_cast<double >(_minX[iDetector]) - 0.5;
    double  xMax = static_cast<double >(_maxX[iDetector]) + 0.5;
    int     yBin = _maxY[iDetector] - _minY[iDetector] + 1;
    double  yMin = static_cast<double >(_minY[iDetector]) - 0.5;
    double  yMax = static_cast<double >(_maxY[iDetector]) + 0.5;
    AIDA::IHistogram2D * hitMapHisto = 
      AIDAProcessor::histogramFactory(this)->createHistogram2D( (basePath + tempHistoName).c_str(),
							       xBin, xMin, xMax,yBin, yMin, yMax);
    _aidaHistoMap.insert(make_pair(tempHistoName, hitMapHisto));
    hitMapHisto->setTitle("Hit map");

  }
  
  
#else
  message<MESSAGE> ( log() << "No histogram produced because Marlin doesn't use AIDA" );
#endif

 
}
