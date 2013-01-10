// Authors
// Antonio Bulgheroni, INFN <mailto:antonio.bulgheroni@gmail.com>
// Joerg Behr, Hamburg Uni/DESY  <joerg.behr@desy.de> 
// Slava Libov, DESY <mailto:vladyslav.libov@desy.de>
// Igor Rubinskiy, DESY <mailto:igorrubinsky@gmail.com>
// 
// Version $Id: EUTelApplyAlignmentProcessor.cc,v 1.17 2009-07-30 17:19:19 jbehr Exp $
/*
 *   This source code is part of the Eutelescope package of Marlin.
 *   You are free to use this source files for your own development as
 *   long as it stays in a public research context. You are not
 *   allowed to use it for commercial purpose. You must put this
 *   header with author names in all development based on this file.
 *   
 */

#ifdef USE_GEAR
// eutelescope includes ".h"
#include "EUTelApplyAlignmentProcessor.h"
#include "EUTelAlignmentConstant.h"
#include "EUTELESCOPE.h"
#include "EUTelEventImpl.h"
#include "EUTelRunHeaderImpl.h"
#include "EUTelHistogramManager.h"
#include "EUTelExceptions.h"
#include "EUTelAPIXSparsePixel.h"
#include "EUTelSparseDataImpl.h"
#include "EUTelAPIXSparseClusterImpl.h"
#include "EUTelVirtualCluster.h"
#include "EUTelFFClusterImpl.h"
#include "EUTelDFFClusterImpl.h"
#include "EUTelBrickedClusterImpl.h"
#include "EUTelSparseClusterImpl.h"
#include "EUTelSparseCluster2Impl.h"
#
// ROOT includes:
#include "TVector3.h"
#include "TVector2.h"
#include "TMatrix.h"



#if defined(USE_AIDA) || defined(MARLIN_USE_AIDA)
// aida includes ".h"
#include <AIDA/IHistogram3D.h>
#include <AIDA/IHistogram2D.h>
#include <AIDA/ITree.h>
#include <AIDA/IHistogramFactory.h>
#include <marlin/AIDAProcessor.h>
#endif

// marlin includes ".h"
#include "marlin/Processor.h"
#include "marlin/Exceptions.h"
#include "marlin/Global.h"

// gear includes <.h>
#include <gear/GearMgr.h>
#include <gear/SiPlanesParameters.h>

// lcio includes <.h>
#include <UTIL/CellIDEncoder.h>
#include <UTIL/CellIDDecoder.h>
#include <EVENT/LCCollection.h>
#include <EVENT/LCEvent.h>
#include <IMPL/LCCollectionVec.h>
//#include <TrackerHitImpl2.h>
#include <IMPL/TrackerHitImpl.h>
#include <IMPL/TrackImpl.h>
#include <IMPL/TrackerDataImpl.h>
#include <IMPL/LCFlagImpl.h>
#include <Exceptions.h>

// ROOT includes ".h"
#include <TVectorD.h>
#include <TMatrixD.h>


// system includes <>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <algorithm>
#include <memory>
#include <string>

using namespace std;
using namespace lcio;
using namespace marlin;
using namespace eutelescope;
using namespace gear;

#if defined(USE_AIDA) || defined(MARLIN_USE_AIDA)
std::string EUTelApplyAlignmentProcessor::_densityPlotBeforeAlignName = "DensityPlotBeforeAlign";
std::string EUTelApplyAlignmentProcessor::_densityPlotAfterAlignName  = "DensityPloAfterAlign";
std::string EUTelApplyAlignmentProcessor::_hitHistoBeforeAlignName    = "HitHistoBeforeAlign";
std::string EUTelApplyAlignmentProcessor::_hitHistoAfterAlignName     = "HitHistoAfterAlign";
#endif

EUTelApplyAlignmentProcessor::EUTelApplyAlignmentProcessor () :Processor("EUTelApplyAlignmentProcessor") {

  // modify processor description
  _description =
    "Apply to the input hit the alignment corrections";

  // first of all we need to register the input collection
  registerInputCollection (LCIO::TRACKERHIT, "InputHitCollectionName",
                           "The name of the input hit collection",
                           internal_inputHitCollectionName, string ("hit"));

  registerInputCollection (LCIO::LCGENERICOBJECT, "AlignmentConstantName",
                           "Alignment constant from the condition file",
                           _alignmentCollectionName, string ("dummy"));

  registerOutputCollection (LCIO::TRACKERHIT, "OutputHitCollectionName",
                            "The name of the output hit collection",
                            _outputHitCollectionName, string("PreAlignedHit"));

  registerOptionalParameter("ReferenceCollection","This is the name of the reference it collection (init at 0,0,0)",
                            internal_referenceHitCollectionName, static_cast< string > ( "reference" ) );

  registerOptionalParameter("OutputReferenceCollection","This is the name of the reference it collection (init at 0,0,0)",
                            _outputReferenceHitCollectionName, static_cast< string > ( "output_refhit" ) );


   registerOptionalParameter("ApplyToReferenceCollection","Do you want the reference hit collection to be corrected by the shifts and tilts from the alignment collection? (default - false )",
                            _applyToReferenceHitCollection, static_cast< bool   > ( false ));
 


  // now the optional parameters
  registerProcessorParameter ("CorrectionMethod",
                              "Available methods are:\n"
                              " 0 --> shift only \n"
                              " 1 --> rotation first \n"
                              " 2 --> shift first ",
                              _correctionMethod, static_cast<int > (1));

  // now the optional parameters
  registerProcessorParameter ("ApplyAlignmentDirection",
                              "Available directinos are:\n"
                              " 0 --> direct  \n"
                              " 1 --> reverse ",
                              _applyAlignmentDirection, static_cast<int > (0));


  // the histogram on / off switch
  registerOptionalParameter("HistogramSwitch","Enable or disable histograms",
                            _histogramSwitch, static_cast< bool > ( 0 ) );

// vector of strings (alignment collections)
  EVENT::StringVec	_alignmentCollectionSuffixExamples;
  _alignmentCollectionSuffixExamples.push_back("alignmentCollectionNames");

  registerProcessorParameter ("alignmentCollectionNames",
                            "List of alignment collections that were applied to the DUT",
                            _alignmentCollectionSuffixes, _alignmentCollectionSuffixExamples);

// vector of strings (hit collections)
  EVENT::StringVec	_hitCollectionSuffixExamples;
  _hitCollectionSuffixExamples.push_back("hitCollectionNames");

  registerProcessorParameter ("hitCollectionNames",
                            "List of hit collections. First one is INPUT collection, every subsequent corresponds to applying alignment collection",
                            _hitCollectionSuffixes, _hitCollectionSuffixExamples);

  EVENT::StringVec	_refhitCollectionSuffixExamples;
  _refhitCollectionSuffixExamples.push_back("hitCollectionNames");

  registerProcessorParameter ("refhitCollectionNames",
                            "List of refhit collections. First one is INPUT collection, every subsequent corresponds to applying alignment collection",
                            _refhitCollectionSuffixes, _refhitCollectionSuffixExamples);


  registerOptionalParameter("DoAlignCollection","Implement geometry shifts and rotations as described in alignmentCollectionName ",
                            _doAlignCollection, static_cast< bool > ( 0 ) );

  registerOptionalParameter("DoGear","Implement geometry shifts and rotations as described in GEAR steering file ",
                            _doGear, static_cast< bool > ( 0 ) );

  registerOptionalParameter("DoAlignmentInOneGo","Apply alignment steps in one go. Is supposed to be used for reversealignment in reverse order, like: undoAlignment, undoPreAlignment, undoGear ",
                            _doAlignmentInOneGo, static_cast< bool > ( 0 ) );

  // DEBUG parameters :
  // turn ON/OFF debug features 
  registerOptionalParameter("DEBUG","Enable or disable DEBUG mode ",
                            _debugSwitch, static_cast< bool > ( 0 ) );
  registerOptionalParameter("Alpha","Rotation Angle around X axis",
                            _alpha, static_cast< double > ( 0.00 ) );
  registerOptionalParameter("Beta","Rotation Angle around Y axis",
                            _beta, static_cast< double > ( 0.00 ) );
  registerOptionalParameter("Gamma","Rotation Angle around Z axis",
                            _gamma, static_cast< double > ( 0.00 ) );
  registerOptionalParameter("PrintEvents", "Events number to have DEBUG1 printed outs (default=10)",
                            _printEvents, static_cast<int> (10) );


  registerOptionalParameter("ReferenceHitFile","This is the name of the reference it collection (init at 0,0,0)",
                            _referenceHitLCIOFile, static_cast< string > ( "reference.slcio" ) );

}

//void EUTelApplyAlignmentProcessor::modifyEvent( LCEvent * /* event */ ){
//  return;
//}

void EUTelApplyAlignmentProcessor::init() {
  // this method is called only once even when the rewind is active
  // usually a good idea to
  printParameters ();

  // set to zero the run and event counters
  _iRun = 0;
  _iEvt = 0;

  // check if the GEAR manager pointer is not null!
  if ( Global::GEAR == 0x0 ) {
    streamlog_out ( ERROR4 ) <<  "The GearMgr is not available, for an unknown reason." << endl;
    exit(-1);
  }

  _siPlanesParameters  = const_cast<SiPlanesParameters* > (&(Global::GEAR->getSiPlanesParameters()));
  _siPlanesLayerLayout = const_cast<SiPlanesLayerLayout*> ( &(_siPlanesParameters->getSiPlanesLayerLayout() ));

  _siPlaneZPosition = new double[ _siPlanesLayerLayout->getNLayers() ];
  for ( int iPlane = 0 ; iPlane < _siPlanesLayerLayout->getNLayers(); iPlane++ ) {
    _siPlaneZPosition[ iPlane ] = _siPlanesLayerLayout->getLayerPositionZ(iPlane);
  }

#if defined(MARLIN_USE_AIDA) || defined(USE_AIDA)
//  _histogramSwitch = false;
#endif
  _isFirstEvent = true;
  _fevent = true;

  _referenceHitVec = 0;
 
  _orderedSensorIDVec.clear();
  for ( int iPlane = 0 ; iPlane < _siPlanesParameters->getSiPlanesNumber() ; ++iPlane ) {
    _orderedSensorIDVec.push_back( _siPlanesLayerLayout->getID( iPlane ) );
  }

  _lookUpTable.clear();
}

//..................................................................................
void EUTelApplyAlignmentProcessor::CheckIOCollections(LCEvent* event)
{

  EUTelEventImpl * evt = static_cast<EUTelEventImpl*> (event);

//        if ( _fevent && _applyToReferenceHitCollection ) 
        {
            try{
            _referenceHitVec       = dynamic_cast < LCCollectionVec * > (event->getCollection( _referenceHitCollectionName));
            }   
            catch(...)
            {
              streamlog_out ( DEBUG ) <<  "_referenceHitCollectionName " << _referenceHitCollectionName.c_str() << " could not be retrieved, creating a dummy one (all elements are null) " << endl;
             
              _referenceHitVec = CreateDummyReferenceHitCollection();
              event->addCollection( _referenceHitVec, _referenceHitCollectionName );
            }

            try
            { 
              _outputReferenceHitVec = dynamic_cast < LCCollectionVec * > (event->getCollection( _outputReferenceHitCollectionName ) );

//              if(_fevent)   streamlog_out ( MESSAGE ) << "found _outputReferenceHitVec "<<  _outputReferenceHitCollectionName.c_str() << " at " << _outputReferenceHitVec << endl ;  
//              if( _outputReferenceHitVec != 0 )
//              {
//                _outputReferenceHitVec->clear();
//                printf("cleared, elements N= %d \n", _outputReferenceHitVec->getNumberOfElements()  );  
//                _outputReferenceHitVec = CreateDummyReferenceHitCollection();
//                event->addCollection( _outputReferenceHitVec, _outputReferenceHitCollectionName );
//              }

            }
            catch(...)
            {
              _outputReferenceHitVec = CreateDummyReferenceHitCollection();
              streamlog_out ( DEBUG ) << "NOT found _outputReferenceHitVec [" << _outputReferenceHitCollectionName.c_str() << "] at " << _outputReferenceHitVec << endl;  
              event->addCollection( _outputReferenceHitVec, _outputReferenceHitCollectionName );
            }

        }
 
  // ----------------------------------------------------------------------- //
  // check input / output collections

  _inputCollectionVec     = 0 ;
  _alignmentCollectionVec = 0 ;
  _outputCollectionVec    = 0 ;

  try 
  {
    _inputCollectionVec         = dynamic_cast < LCCollectionVec* > (evt->getCollection(_inputHitCollectionName));
    streamlog_out  ( DEBUG ) <<  " input ["<< _inputHitCollectionName <<"] collection found on event " << event->getEventNumber() << endl;
    streamlog_out  ( DEBUG ) <<  " input ["<< _inputHitCollectionName <<"] collection found at       " << _inputCollectionVec << endl;  
  } catch (DataNotAvailableException& e) {
    streamlog_out  ( DEBUG ) <<  "No input ["<< _inputHitCollectionName <<"] collection found on event " << event->getEventNumber()
                                << " in run " << event->getRunNumber() << endl;
  }
 
  if( _alignmentCollectionName != "gear" )
  {
    try 
    {
      _alignmentCollectionVec     = dynamic_cast < LCCollectionVec* > (evt->getCollection(_alignmentCollectionName));
      streamlog_out  ( DEBUG ) <<  "found Alignment ["<< _alignmentCollectionName <<"] collection found on event " << event->getEventNumber() <<
                                   " at " << _alignmentCollectionVec << endl;
   } catch (DataNotAvailableException& e) {
      streamlog_out  ( DEBUG ) <<  "No Alignment ["<< _alignmentCollectionName <<"] collection found on event " << event->getEventNumber()
                                  << " in run " << event->getRunNumber() << endl;
    }
  }
  
  try{
      _outputCollectionVec = dynamic_cast < LCCollectionVec * > (evt->getCollection(_outputHitCollectionName));
      if(_outputCollectionVec == 0)
      {
        _outputCollectionVec = new LCCollectionVec(LCIO::TRACKERHIT);
        evt->addCollection( _outputCollectionVec, _outputHitCollectionName );
        streamlog_out  ( DEBUG ) <<  " created new " <<  _outputHitCollectionName  << endl;
      } 
      else
      {
        streamlog_out  ( DEBUG ) <<  " opened " <<  _outputHitCollectionName  << endl;
      }
  }catch(...){
      _outputCollectionVec = new LCCollectionVec(LCIO::TRACKERHIT);
      evt->addCollection( _outputCollectionVec, _outputHitCollectionName );
      streamlog_out  ( DEBUG ) <<  " try catch. nevertheless created new " <<  _outputHitCollectionName  << endl;
  }
  // ------------------------------------------------------------------------- //


}

//..................................................................................
void EUTelApplyAlignmentProcessor::processRunHeader (LCRunHeader * runHeader)
{

//  auto_ptr<EUTelRunHeaderImpl> runHeader ( new EUTelRunHeaderImpl( rdr ) ) ;
//  runHeader->addProcessor( type() );
//
//  // increment the run counter
//  ++_iRun;

  auto_ptr<EUTelRunHeaderImpl> eutelHeader( new EUTelRunHeaderImpl ( runHeader ) );
  eutelHeader->addProcessor( type() );


  _nRun++ ;

  // Decode and print out Run Header information - just a check

  int runNr = runHeader->getRunNumber();
  // convert to string
  char buf[256];
  sprintf(buf, "%i", runNr);
  std::string runNr_str(buf);

  message<MESSAGE> ( log() << "Processing run header " << _nRun
                     << ", run number " << runNr );

  const std::string detectorName = runHeader->getDetectorName();
  const std::string detectorDescription = runHeader->getDescription();
  //  const std::vector<std::string> * subDets = runHeader->getActiveSubdetectors();

  message<MESSAGE> ( log() << detectorName << " : " << detectorDescription ) ;

  // pick up correct alignment collection
  _alignmentCollectionNames.clear();
  _hitCollectionNames.clear();
  _refhitCollectionNames.clear();

  if( _alignmentCollectionSuffixes.size() < 1 )
  {
      message<MESSAGE> ( log() << "You must define at least one Alignmen Collection to start ApplyAlignment Processor" << endl ) ;
      throw StopProcessingException(this);
  }
  else 
  if( _alignmentCollectionSuffixes.size() == 1 )
  {
    message<MESSAGE> ( log() << "Only one Alignment Collection has been specified" << endl ) ;
    message<MESSAGE> ( log() << "Ignore other Suffix collections, proceed with the defaults given by _referenceHitCollectionName and _inputHitCollectionName " <<endl );
    _alignmentCollectionNames.push_back( _alignmentCollectionSuffixes[0] );
    _refhitCollectionNames.push_back( internal_referenceHitCollectionName );
    _hitCollectionNames.push_back( _outputHitCollectionName );

    message<MESSAGE> ( log() << " _alignmentCollectionNames[0] = " << _alignmentCollectionNames[0]  << endl);
    message<MESSAGE> ( log() << " _hitCollectionNames[0] = " << _hitCollectionNames[0]  << endl);
    message<MESSAGE> ( log() << " _refhitCollectionNames[0] = " << _refhitCollectionNames[0]  << endl);

//    _outputReferenceHitCollectionName =  _refhitCollectionNames[0];
  }
  else
  if(
     ( _alignmentCollectionSuffixes.size() != _hitCollectionSuffixes.size() )
     ||

    ( _alignmentCollectionSuffixes.size() != _refhitCollectionSuffixes.size() )
     ||

    ( _hitCollectionSuffixes.size() != _refhitCollectionSuffixes.size() )
     )
   {
    
    message<ERROR  > ( log() << endl );
    message<ERROR  > ( log() << " ***************************************************************************" );    
    message<ERROR  > ( log() << " ***          dont't mess with the steering cards                        ***" );    
    message<ERROR  > ( log() << " ***                                                                     ***" );    
    message<ERROR  > ( log() << endl );
 
    for (unsigned i = 0; i < _alignmentCollectionSuffixes.size(); i++) 
    {
      message<ERROR  > ( log() << " _alignmentCollectionSuffixes["<<i<<"] : " << _alignmentCollectionSuffixes[i] );    
    }
    for (unsigned i = 0; i < _hitCollectionSuffixes.size(); i++) 
    {
      message< ERROR > ( log() << " _hitCollectionSuffixes["<<i<<"] : " << _hitCollectionSuffixes[i]  );    
    }
    for (unsigned i = 0; i < _refhitCollectionSuffixes.size(); i++) 
    {
      message< ERROR > ( log() << " _refhitCollectionSuffixes["<<i<<"] : " << _refhitCollectionSuffixes[i]  );    
    }
 
     message<ERROR  > ( log() << endl );
     TString ExceptionMessage = " Input collection names are inconsistent in number of elements: \n \n";
     ExceptionMessage += " alignment [ "; ExceptionMessage += _alignmentCollectionSuffixes.size();  ExceptionMessage += "]\n";
     ExceptionMessage += " hit [ ";       ExceptionMessage += _hitCollectionSuffixes.size();        ExceptionMessage += "]\n";
     ExceptionMessage += " refhit [ ";    ExceptionMessage += _refhitCollectionSuffixes.size();     ExceptionMessage += "]\n";
     message<ERROR> ( log() << ExceptionMessage.Data()  );
//     throw InvalidParameterException( ExceptionMessage.Data() );
//     message<ERROR  > ( log() << endl );
     message<ERROR  > ( log() << " ***                                                                     ***" );    
     message<ERROR  > ( log() << " ***************************************************************************" );    
     message<ERROR  > ( log() << endl );

     throw StopProcessingException(this);
   }
   else
   {
     for (unsigned i = 0; i < _alignmentCollectionSuffixes.size(); i++) 
     {     
       std::string	alitemp = _alignmentCollectionSuffixes[i];        
       _alignmentCollectionNames.push_back(alitemp);
       std::string	hittemp = _hitCollectionSuffixes[i];
       _hitCollectionNames.push_back(hittemp);
       std::string	reftemp = _refhitCollectionSuffixes[i];
       _refhitCollectionNames.push_back(reftemp);
	//cout << _alignmentCollectionNames[i] << endl;
     }
   }

}

//........................................................................................................................
void EUTelApplyAlignmentProcessor::processEvent (LCEvent * event) {

    if( _alignmentCollectionNames.size() <= 0 )
    {
        streamlog_out ( ERROR ) << "Alignment collections are UNDEFINED, the processor can not continue. EXIT " << endl;
        throw StopProcessingException(this);       
    }
    else
    {    
 
        if ( _fevent )
            streamlog_out ( MESSAGE4 ) << "Processing run "  
                << setw(6) << setiosflags(ios::right) << setfill('0')  << event->getRunNumber()
                << ". Number of the defined alignment collection is " << _alignmentCollectionNames.size()
                << endl;
        

  // ----------------------------------------------------------------------- //
  // check input / output collections

  //
  //......................................................................  //

        for (unsigned int i = _alignmentCollectionNames.size() -1 ; i >= 0; i-- ) 
        {
            // read the first available alignment collection
            // CAUTION 1: it might be important to keep the order of alignment collections (if many) given in the opposite direction
            // CAUTION 2: to be controled via steering files
            //


            _alignmentCollectionName    = _alignmentCollectionNames.at(i);
//            _referenceHitCollectionName = _refhitCollectionNames.at(i);
//            _inputHitCollectionName     = _hitCollectionNames.at(i);

            if( i ==  _alignmentCollectionNames.size() -1 ) 
            {
               _inputHitCollectionName     = internal_inputHitCollectionName     ; 
               _referenceHitCollectionName = internal_referenceHitCollectionName ; 
/*              if( _inputHitCollectionName != _hitCollectionNames.at(i)  )   
                {
                    _inputHitCollectionName  = _hitCollectionNames.at(i);
                }
                if( _referenceHitCollectionName != _refhitCollectionNames.at(i)  )   
                {
                    _referenceHitCollectionName  = _refhitCollectionNames.at(i);
                }*/
                  _outputHitCollectionName          = _hitCollectionNames.at(i);//_input_inputHitCollectionName + _alignmentCollectionName; 
                  _outputReferenceHitCollectionName = _refhitCollectionNames.at(i);//referenceH_referenceHitCollectionName + _alignmentCollectionName; 
               
            } 
            else
               if( i >= 0 ) 
               {
//               std::string align1 = _alignmentCollectionNames.at(i+1);
//               int align_length1 = align1.length();

//               int outputhit_length2 = _outputHitCollectionName.length();
//               int outputREFhit_length2 = _outputReferenceHitCollectionName.length();

//               std::string temp = "";
//               temp =  _inputHitCollectionName;
//               temp.erase( outputhit_length2 - align_length1, align_length1);
                 _inputHitCollectionName           = _hitCollectionNames.at(i+1);
                 _outputHitCollectionName          = _hitCollectionNames.at(i);//temp + _alignmentCollectionName; 

//               temp = _referenceHitCollectionName;
//               temp.erase( outputREFhit_length2 - align_length1, align_length1);
                 _referenceHitCollectionName       = _refhitCollectionNames.at(i+1);
                 _outputReferenceHitCollectionName = _refhitCollectionNames.at(i);//temp + _alignmentCollectionName; 
               }  
 
//            printf("[%2d of %2d]  alignment: [%s] ", i,  _alignmentCollectionNames.size(),  _alignmentCollectionName.c_str() );
//            printf("collections: input [%s] output [%s] refhit in [%s] out [%s] \n", 
//                    _inputHitCollectionName.c_str(), _outputHitCollectionName.c_str() , 
//                    _referenceHitCollectionName.c_str(), _outputReferenceHitCollectionName.c_str() );
 
  CheckIOCollections(event);

//            streamlog_out ( MESSAGE4 ) << "_doAlignmentInOneGo == " << _doAlignmentInOneGo << endl;              

            if( _doAlignmentInOneGo != true )  // keep for backward compatibility , hopefully to declare OBSOLETE soon
            {
              if( GetApplyAlignmentDirection() == 0 )
              {
                if( _doGear )
                {
                    ApplyGear6D(event);
                }
                if( _doAlignCollection )
                {
                    Direct(event);
                }
              }
              else
                if( GetApplyAlignmentDirection() == 1 )
                {
                    if( _doAlignCollection )
                    {
                        Reverse(event);     
                    }
                    if( _doGear )
                    {
                        RevertGear6D(event);
                    }
                }
                else
                {
                    throw StopProcessingException(this); 
                }
            }
            else
                if( _doAlignmentInOneGo == true )  // logic from 01.09.2012 . hopefully to declare PRO soon
                {                   
//                      streamlog_out ( MESSAGE4 ) << "_doAlignmentInOneGo == true " << endl;              

                      if( _alignmentCollectionName == "gear" )
                         {
                           if( GetApplyAlignmentDirection() == 1 )
                           {
                             RevertGear6D(event);                    
                           }
                           else if( GetApplyAlignmentDirection() == 0 )
                           {
                             ApplyGear6D(event);                    
                           }
                         }
                       else
                         {
                           if( GetApplyAlignmentDirection() == 1 )
                           {
                             Reverse(event);
                           }
                           else if( GetApplyAlignmentDirection() == 0 )
                           {
                             Direct(event);
                           }
                         }  
                }
                else
                    {
                       streamlog_out ( MESSAGE4 ) << "Man! you MUST specifiy whether you want the alignment to be done in one go or not" << endl;              
                       streamlog_out ( MESSAGE4 ) << "Check your steering cards!!" << endl;              
                    }
        }
 
        if(_fevent)
        {
            _isFirstEvent = false;
            _fevent = false;
        }
        _iEvt++; 
    }
/* 
    if ( _applyToReferenceHitCollection ) 
    {
      LCCollectionVec * ref    = static_cast < LCCollectionVec * > (event->getCollection( _referenceHitCollectionName ));
      for(size_t ii = 0 ; ii <  ref->getNumberOfElements(); ii++)
      {
        EUTelReferenceHit * refhit = static_cast< EUTelReferenceHit*> ( ref->getElementAt(ii) ) ;
        printf("FIN sensorID: %5d dx:%5.3f dy:%5.3f dz:%5.3f  alfa:%5.3f beta:%5.3f gamma:%5.3f \n",
        refhit->getSensorID(   ),                      
        refhit->getXOffset(    ),
        refhit->getYOffset(    ),
        refhit->getZOffset(    ),
        refhit->getAlpha(),
        refhit->getBeta(),
        refhit->getGamma()    );
      }
    }
*/

    if ( _applyToReferenceHitCollection ) 
    {
     for( size_t jj = 0 ; jj < _alignmentCollectionNames.size(); jj ++)
     { 
       std::string _colName = _alignmentCollectionNames.at(jj);
       LCCollectionVec * ref    = static_cast < LCCollectionVec * > (event->getCollection( _colName  ));
       for( size_t ii = 0 ; ii <  (unsigned int)ref->getNumberOfElements(); ii++)
       {
        streamlog_out( DEBUG ) << " check output_refhit at : " << ref << " ";
        EUTelReferenceHit* output_refhit = static_cast< EUTelReferenceHit*> ( ref->getElementAt(ii) ) ;
        streamlog_out( DEBUG ) << " at : " <<  output_refhit << endl;     
        streamlog_out( DEBUG ) << "CHK sensorID: " <<  output_refhit->getSensorID(   )     
                              << " x    :" <<        output_refhit->getXOffset(    )    
                              << " y    :" <<        output_refhit->getYOffset(    )    
                              << " z    :" <<        output_refhit->getZOffset(    )    
                              << " alfa :" <<        output_refhit->getAlpha()          
                              << " beta :" <<        output_refhit->getBeta()           
                              << " gamma:" <<        output_refhit->getGamma()        << endl ;
       }
     }
    }
}

void EUTelApplyAlignmentProcessor::ApplyGear6D( LCEvent *event) 
{
 

  if ( _iEvt % 1000 == 0 )
    streamlog_out ( MESSAGE4 ) << "Processing event  (ApplyGear6D) "
                               << setw(6) << setiosflags(ios::right) << event->getEventNumber() << " in run "
                               << setw(6) << setiosflags(ios::right) << setfill('0')  << event->getRunNumber()
                               << setfill(' ')
                               << " (Total = " << setw(10) << _iEvt << ")" << resetiosflags(ios::left) << endl;
//++_iEvt;


  EUTelEventImpl * evt = static_cast<EUTelEventImpl*> (event);

  if ( evt->getEventType() == kEORE ) 
  {
    streamlog_out ( DEBUG4 ) << "EORE found: nothing else to do." << endl;
    return;
  }
  else if ( evt->getEventType() == kUNKNOWN ) 
  {
    streamlog_out ( WARNING2 ) << "Event number " << evt->getEventNumber() << " in run " << evt->getRunNumber()
                               << " is of unknown type. Continue considering it as a normal Data Event." << endl;
  }


     
//    if (_fevent) 
    {    
#ifndef NDEBUG
        // print out the lookup table
        map< int , int >::iterator mapIter = _lookUpTable[ _alignmentCollectionName ].begin();
        while ( mapIter != _lookUpTable[ _alignmentCollectionName ].end() ) 
        {
            streamlog_out ( DEBUG ) << "Sensor ID = " << mapIter->first
                                    << " is in position " << mapIter->second << endl;
            ++mapIter;
        }
#endif
    }
    
    
    if( _inputCollectionVec == 0 )
    {
      streamlog_out ( DEBUG ) << "EUTelApplyAlignmentProcessor::ApplyGear6D. Skip this event. Input Collection not found. " << endl;  
      return;
    }

    for (size_t iHit = 0; iHit < _inputCollectionVec->size(); iHit++) 
    {

      TrackerHitImpl   * inputHit   = dynamic_cast< TrackerHitImpl * >  ( _inputCollectionVec->getElementAt( iHit ) ) ;

      // now we have to understand which layer this hit belongs to.
      int sensorID = guessSensorID( inputHit );

      if ( _conversionIdMap.size() != (unsigned) _siPlanesParameters->getSiPlanesNumber() ) 
      {
          // first of all try to see if this sensorID already belong to
          if ( _conversionIdMap.find( sensorID ) == _conversionIdMap.end() ) 
          {
              // this means that this detector ID was not already inserted,
              // so this is the right place to do that
          
              for ( int iLayer = 0; iLayer < _siPlanesLayerLayout->getNLayers(); iLayer++ ) 
              {
                  if ( _siPlanesLayerLayout->getID(iLayer) == sensorID ) 
                  {
                      _conversionIdMap.insert( make_pair( sensorID, iLayer ) );
                      break;
                  }
              }
          }
      }

      int layerIndex   = _conversionIdMap[sensorID];

      // determine z position of the plane
	  // 20 December 2010 @libov
      float	z_sensor = 0;
	  for ( int iPlane = 0 ; iPlane < _siPlanesLayerLayout->getNLayers(); ++iPlane ) 
      {
          if (sensorID == _siPlanesLayerLayout->getID( iPlane ) ) 
          {
              z_sensor = _siPlanesLayerLayout -> getSensitivePositionZ( iPlane ) + 0.5 * _siPlanesLayerLayout->getSensitiveThickness( iPlane );
              break;
          }
      }

      // copy the input to the output, at least for the common part
      TrackerHitImpl   * outputHit  = new TrackerHitImpl;
      outputHit->setType( inputHit->getType() );
      outputHit->rawHits() = inputHit->getRawHits();


      double * inputPosition      = const_cast< double * > ( inputHit->getPosition() ) ;
      double   outputPosition[3]  = { 0., 0., 0. };

      if ( 1==1 ) 
      {

          double telPos[3]    = {0., 0., 0.};
          double gRotation[3] = { 0., 0., 0.}; // not rotated

          if ( _debugSwitch )
          {
              telPos[0] = 0.;
              telPos[1] = 0.;
              telPos[2] = 0.; 
              gRotation[0] = _alpha;
              gRotation[1] = _beta ;
              gRotation[2] = _gamma;
          }
          else
          {
              gRotation[0]    = _siPlanesLayerLayout->getLayerRotationXY(layerIndex); // Euler alpha ;
              gRotation[1]    = _siPlanesLayerLayout->getLayerRotationZX(layerIndex); // Euler alpha ;
              gRotation[2]    = _siPlanesLayerLayout->getLayerRotationZY(layerIndex); // Euler alpha ;

              // input angles are in DEGREEs !!!
              // translate into radians
    
              gRotation[0]  =   gRotation[0] *3.1415926/180.; // 
              gRotation[1]  =   gRotation[1] *3.1415926/180.; //
              gRotation[2]  =   gRotation[2] *3.1415926/180.; //

              telPos[0]  =  _siPlanesLayerLayout->getSensitivePositionX(layerIndex); // mm
              telPos[1]  =  _siPlanesLayerLayout->getSensitivePositionY(layerIndex); // mm
              telPos[2]  =  _siPlanesLayerLayout->getSensitivePositionZ(layerIndex); // mm
              
          }
 
      
          if( _iEvt < _printEvents )
          {
                if ( _debugSwitch ) 
                {
                    streamlog_out ( MESSAGE )  << "Debugmode ON " << endl;                                   
                }
                
                streamlog_out ( MESSAGE )  << "_applyGear6D " << endl;
                streamlog_out ( MESSAGE )  << " telPos[0] = " << telPos[0]  << endl;
                streamlog_out ( MESSAGE )  << " telPos[1] = " << telPos[1]  << endl;
                streamlog_out ( MESSAGE )  << " telPos[2] = " << telPos[2]  << endl;
                streamlog_out ( MESSAGE )  << " gRotation[0] = " << gRotation[0]  << endl;
                streamlog_out ( MESSAGE )  << " gRotation[1] = " << gRotation[1]  << endl;
                streamlog_out ( MESSAGE )  << " gRotation[2] = " << gRotation[2]  << endl;
          }

          // rotations first
 
          outputPosition[0] = inputPosition[0];
          outputPosition[1] = inputPosition[1];
          outputPosition[2] = inputPosition[2] - z_sensor;
          
          _EulerRotation( sensorID,outputPosition, gRotation);

          // then the shifts
//          outputPosition[0]  = telPos[0];
//          outputPosition[1]  = telPos[1];
//          outputPosition[2]  = telPos[2];

          outputPosition[2] += z_sensor;
      }
      else
      {
          // this hit belongs to a plane whose sensorID is not in the
          // alignment constants. So the idea is to eventually advice
          // the users if running in DEBUG and copy the not aligned hit
          // in the new collection.
//          streamlog_out ( DEBUG3 ) << "Sensor ID " << sensorID << " not found. Skipping alignment for hit "
//                                    << iHit << endl;

          for ( size_t i = 0; i < 3; ++i )
          {
              outputPosition[i] = inputPosition[i];
          }
          
      }

      if ( _iEvt < _printEvents )
      {
         streamlog_out ( MESSAGE ) << "ApplyGear: INPUT: Sensor ID " << sensorID << " " << inputPosition[0] << " " << inputPosition[1] << " " << inputPosition[2] << " " << z_sensor << endl;                
         streamlog_out ( MESSAGE ) << "ApplyGear: OUTPUT:Sensor ID " << sensorID << " " << outputPosition[0] << " " << outputPosition[1] << " " << outputPosition[2] << " " << z_sensor << endl;                
      }

       outputHit->setPosition( outputPosition ) ;
      _outputCollectionVec->push_back( outputHit );
    }

}


void EUTelApplyAlignmentProcessor::RevertGear6D( LCEvent *event) 
{
 
  if ( _iEvt % 1000 == 0 )
    streamlog_out ( MESSAGE4 ) << "Processing event  (RevertGear6D) "
                               << setw(6) << setiosflags(ios::right) << event->getEventNumber() << " in run "
                               << setw(6) << setiosflags(ios::right) << setfill('0')  << event->getRunNumber()
                               << setfill(' ')
                               << " (Total = " << setw(10) << _iEvt << ")" << resetiosflags(ios::left) << endl;
//  ++_iEvt;


  EUTelEventImpl * evt = static_cast<EUTelEventImpl*> (event);

  if ( evt->getEventType() == kEORE ) 
  {
    streamlog_out ( DEBUG4 ) << "EORE found: nothing else to do." << endl;
    return;
  }
  else if ( evt->getEventType() == kUNKNOWN ) 
  {
    streamlog_out ( WARNING2 ) << "Event number " << evt->getEventNumber() << " in run " << evt->getRunNumber()
                               << " is of unknown type. Continue considering it as a normal Data Event." << endl;
  }


  // ----------------------------------------------------------------------- //
  // check input / output collections

//  CheckIOCollections(event);

  //
  // ----------------------------------------------------------------------- //
    
//    if (_fevent) 
    {     
#ifndef NDEBUG
        // print out the lookup table
        map< int , int >::iterator mapIter = _lookUpTable[ _alignmentCollectionName ].begin();
        while ( mapIter != _lookUpTable[ _alignmentCollectionName ].end() ) 
        {
            streamlog_out ( DEBUG ) << "Sensor ID = " << mapIter->first
                                    << " is in position " << mapIter->second << endl;
            ++mapIter;
        }
#endif
    }
    
// final check
    if( _inputCollectionVec == 0 )
    {
      streamlog_out ( DEBUG ) << "EUTelApplyAlignmentProcessor::RevertGear6D. Skip this event. Input Collection not found. " << endl;  
      return;
    }

// go-go
    for (size_t iHit = 0; iHit < _inputCollectionVec->size(); iHit++) 
    {

      TrackerHitImpl   * inputHit   = dynamic_cast< TrackerHitImpl * >  ( _inputCollectionVec->getElementAt( iHit ) ) ;

      // now we have to understand which layer this hit belongs to.
      int sensorID = guessSensorID( inputHit );

      if ( _conversionIdMap.size() != (unsigned) _siPlanesParameters->getSiPlanesNumber() ) 
      {
          // first of all try to see if this sensorID already belong to
          if ( _conversionIdMap.find( sensorID ) == _conversionIdMap.end() ) 
          {
              // this means that this detector ID was not already inserted,
              // so this is the right place to do that
          
              for ( int iLayer = 0; iLayer < _siPlanesLayerLayout->getNLayers(); iLayer++ ) 
              {
                  if ( _siPlanesLayerLayout->getID(iLayer) == sensorID ) 
                  {
                      _conversionIdMap.insert( make_pair( sensorID, iLayer ) );
                      break;
                  }
              }
          }
      }
      
      int layerIndex   = _conversionIdMap[sensorID];

      // determine z position of the plane
      // 20 December 2010 @libov
/*      float	z_sensor = 0;
      for ( int iPlane = 0 ; iPlane < _siPlanesLayerLayout->getNLayers(); ++iPlane ) 
      {
          if (sensorID == _siPlanesLayerLayout->getID( iPlane ) ) 
          {
              z_sensor = _siPlanesLayerLayout -> getSensitivePositionZ( iPlane ) + 0.5 * _siPlanesLayerLayout->getSensitiveThickness( iPlane );
              break;
          }
      }
*/

// retrieve the refhit cooridantes (eventual offset of the sensor) 
      double x_refhit = 0.; 
      double y_refhit = 0.; 
      double z_refhit = 0.; 
      
      if( _referenceHitVec == 0 )
      {
	// todo: is this case (no reference vector) treated correctly?
	  //streamlog_out(MESSAGE) << "_referenceHitVec is empty" << endl;
      }
      else
      {
//        streamlog_out(MESSAGE) << "reference Hit collection name : " << _referenceHitCollectionName << endl;
 
       for(size_t ii = 0 ; ii <  (unsigned int)_referenceHitVec->getNumberOfElements(); ii++)
       {
        EUTelReferenceHit * refhit = static_cast< EUTelReferenceHit*> ( _referenceHitVec->getElementAt(ii) ) ;
        if( sensorID != refhit->getSensorID() )
        {
       //   streamlog_out(MESSAGE) << "Looping through a varity of sensor IDs" << endl;
          continue;
        }
        else
        {
          x_refhit =  refhit->getXOffset();
          y_refhit =  refhit->getYOffset();
          z_refhit =  refhit->getZOffset();

          if( _iEvt < _printEvents )
          {
            streamlog_out(MESSAGE) << "Sensor ID and Alignment plane ID match!" << endl;
            streamlog_out(MESSAGE) << "x_refhit " << x_refhit  << endl; 
            streamlog_out(MESSAGE) << "y_refhit " << y_refhit  << endl; 
            streamlog_out(MESSAGE) << "z_refhit " << z_refhit  << endl; 
          }
          break;
        }
       }
      }

      // copy the input to the output, at least for the common part
      TrackerHitImpl   * outputHit  = new TrackerHitImpl;
      outputHit->setType( inputHit->getType() );
      outputHit->rawHits() = inputHit->getRawHits();


      const double * inputPosition      = const_cast< const double * > ( inputHit->getPosition() ) ;
      double   outputPosition[3]  = { 0., 0., 0. };

   
      if ( 1==1  ) 
      {

          double telPos[3]    = {0., 0., 0.};
          double gRotation[3] = { 0., 0., 0.}; // not rotated


          if ( _debugSwitch )
          {
              telPos[0] = 0.;
              telPos[1] = 0.;
              telPos[2] = 0.; 
              gRotation[0] = _alpha;
              gRotation[1] = _beta ;
              gRotation[2] = _gamma;
          }
          else
          {
              gRotation[0]    = _siPlanesLayerLayout->getLayerRotationXY(layerIndex); // Euler alpha ;
              gRotation[1]    = _siPlanesLayerLayout->getLayerRotationZX(layerIndex); // Euler alpha ;
              gRotation[2]    = _siPlanesLayerLayout->getLayerRotationZY(layerIndex); // Euler alpha ;

              // input angles are in DEGREEs !!!
              // translate into radians
    
              gRotation[0]  =   gRotation[0] *3.1415926/180.; // 
              gRotation[1]  =   gRotation[1] *3.1415926/180.; //
              gRotation[2]  =   gRotation[2] *3.1415926/180.; //

              telPos[0]  =  _siPlanesLayerLayout->getSensitivePositionX(layerIndex); // mm
              telPos[1]  =  _siPlanesLayerLayout->getSensitivePositionY(layerIndex); // mm
              telPos[2]  =  _siPlanesLayerLayout->getSensitivePositionZ(layerIndex)+0.5*_siPlanesLayerLayout->getSensitiveThickness(layerIndex) ; // mm
              
          }
      
          if( _iEvt < _printEvents )
          {
                if ( _debugSwitch ) 
                {
                    streamlog_out ( MESSAGE )  << "Debugmode ON " << endl;                                   
                }
                
                streamlog_out ( MESSAGE )  << "_revertGear6D " << endl;
                streamlog_out ( MESSAGE )  << " telPos[0] = " << telPos[0]  << endl;
                streamlog_out ( MESSAGE )  << " telPos[1] = " << telPos[1]  << endl;
                streamlog_out ( MESSAGE )  << " telPos[2] = " << telPos[2]  << endl;
                streamlog_out ( MESSAGE )  << " gRotation[0] = " << gRotation[0]  << endl;
                streamlog_out ( MESSAGE )  << " gRotation[1] = " << gRotation[1]  << endl;
                streamlog_out ( MESSAGE )  << " gRotation[2] = " << gRotation[2]  << endl;
          }
// new implmentation // Rubinskiy 11.11.11

// undo the shifts = go back to the center of the sensor frame (rotations unchanged)
      outputPosition[0] = inputPosition[0] - x_refhit;
      outputPosition[1] = inputPosition[1] - y_refhit;
      outputPosition[2] = inputPosition[2] - z_refhit;

      if ( _iEvt < _printEvents )
      {
         streamlog_out ( MESSAGE ) << "RevertGear: intermediate: "<<outputPosition[0] << " " <<outputPosition[1] << " " <<outputPosition[2] <<  endl;
      } 
      TVector3 iCenterOfSensorFrame( outputPosition[0], outputPosition[1], outputPosition[2] );

      iCenterOfSensorFrame.RotateZ( -gRotation[0] );
      iCenterOfSensorFrame.RotateY( -gRotation[1] );
      iCenterOfSensorFrame.RotateX( -gRotation[2] ); // first rotaiton in ZY plane -> around X axis (gamma)

      outputPosition[0] = iCenterOfSensorFrame(0);
      outputPosition[1] = iCenterOfSensorFrame(1);
      outputPosition[2] = iCenterOfSensorFrame(2);

         
          
/* old implementation
          // rotations first
 
          outputPosition[0] = inputPosition[0];
          outputPosition[1] = inputPosition[1];
          outputPosition[2] = inputPosition[2] - z_sensor;
          
          _EulerRotationInverse( sensorID,outputPosition, gRotation);

          // then the shifts
//          outputPosition[0] -= telPos[0];
//          outputPosition[1] -= telPos[1];
//          outputPosition[2] -= telPos[2];

          outputPosition[2] += z_sensor;
*/
      }
      else
      {
          // this hit belongs to a plane whose sensorID is not in the
          // alignment constants. So the idea is to eventually advice
          // the users if running in DEBUG and copy the not aligned hit
          // in the new collection.
          streamlog_out ( DEBUG ) << "Sensor ID " << sensorID << " not found. Skipping alignment for hit "
                                    << iHit << endl;

          for ( size_t i = 0; i < 3; ++i )
          {
              outputPosition[i] = inputPosition[i];
          }
          
      }

      if ( _iEvt < _printEvents )
      {
        streamlog_out ( MESSAGE ) << "RevertGear: INPUT: Sensor ID " << sensorID << " " << inputPosition[0] << " " << inputPosition[1] << " " << inputPosition[2] <<  endl;
        streamlog_out ( MESSAGE ) << "RevertGear: OUTPUT:Sensor ID " << sensorID << " " << outputPosition[0] << " " << outputPosition[1] << " " << outputPosition[2] << endl;
      }

      outputHit->setPosition( outputPosition ) ;
      _outputCollectionVec->push_back( outputHit );
 
      TransformToLocalFrame(outputHit,evt); 
    }

}

void EUTelApplyAlignmentProcessor::Direct(LCEvent *event) {

  if ( _iEvt % 1000 == 0 )
    streamlog_out ( MESSAGE4 ) << "Processing event  (ApplyAlignment Direct) "
                               << setw(6) << setiosflags(ios::right) << event->getEventNumber() << " in run "
                               << setw(6) << setiosflags(ios::right) << setfill('0')  << event->getRunNumber()
                               << setfill(' ')
                               << " (Total = " << setw(10) << _iEvt << ")" << resetiosflags(ios::left) << endl;
//  ++_iEvt;

  EUTelEventImpl * evt = static_cast<EUTelEventImpl*> (event);

  if ( evt->getEventType() == kEORE ) 
  {
    streamlog_out ( DEBUG4 ) << "EORE found: nothing else to do." << endl;
    return;
  }
  else if ( evt->getEventType() == kUNKNOWN ) 
  {
    streamlog_out ( WARNING2 ) << "Event number " << evt->getEventNumber() << " in run " << evt->getRunNumber()
                               << " is of unknown type. Continue considering it as a normal Data Event." << endl;
  }


  // ----------------------------------------------------------------------- //
  // check input / output collections

  // CheckIOCollections(event);

  //
  // ----------------------------------------------------------------------- //

    if (_fevent) 
    {
        streamlog_out ( DEBUG   ) << "DIRECT:FIRST:REFHIT: The alignment collection ["<< _alignmentCollectionName.c_str() <<"] contains: " <<  _alignmentCollectionVec->getNumberOfElements() << " planes " << endl;    
    
        if(_alignmentCollectionVec->size() > 0 )
        {
            streamlog_out ( DEBUG   ) << "DIRECT:FIRST:REFHIT: alignment sensorID: " ;
 
            for ( size_t iPos = 0; iPos < _alignmentCollectionVec->size(); ++iPos ) 
            {
                EUTelAlignmentConstant * alignment = static_cast< EUTelAlignmentConstant * > ( _alignmentCollectionVec->getElementAt( iPos ) );
                _lookUpTable[ _alignmentCollectionName ][ alignment->getSensorID() ] = iPos;
                streamlog_out ( MESSAGE ) << " _alignmentCollectionVec element position : " << iPos << " " ;
            
                if ( _applyToReferenceHitCollection ) 
                {
                 AlignReferenceHit( evt,   alignment); 
                }
            }
            streamlog_out ( MESSAGE ) << endl;
//            if ( _applyToReferenceHitCollection ) 
//            {
//            }
        }

        if ( _histogramSwitch )
        {          
            bookHistos();
        }
          
        streamlog_out ( DEBUG   ) << "DIRECT:FIRST:REFHIT: The alignment collection ["<< _alignmentCollectionName.c_str() <<"] contains: " <<  _alignmentCollectionVec->getNumberOfElements()
                                  << " planes " << endl;   

#ifndef NDEBUG
        // print out the lookup table
        map< int , int >::iterator mapIter = _lookUpTable[ _alignmentCollectionName ].begin();
        while ( mapIter != _lookUpTable[ _alignmentCollectionName ].end() ) 
        {
            streamlog_out ( DEBUG ) << "DIRECT:FIRST:REFHIT: Sensor ID = " << mapIter->first
                                    << " is in position " << mapIter->second << endl;
            ++mapIter;
        }
#endif
    }


// final check
    if( _inputCollectionVec == 0 )
    {
      streamlog_out ( DEBUG ) << "DIRECT:-----:-----: EUTelApplyAlignmentProcessor::Direct. Skip this event. Input Collection not found. " << endl;  
      return;
    }

    streamlog_out ( DEBUG ) << "DIRECT:-----:-----: EUTelApplyAlignmentProcessor::Direct. going to proceeed with " <<  _inputCollectionVec->size() << " hits " << endl;

// go-go
    for (size_t iHit = 0; iHit < _inputCollectionVec->size(); iHit++) {

      streamlog_out ( DEBUG ) << "DIRECT:-----:-----: hit " << iHit << " of  " <<  _inputCollectionVec->size() << " hits " << endl;

      TrackerHitImpl   * inputHit   = dynamic_cast< TrackerHitImpl * >  ( _inputCollectionVec->getElementAt( iHit ) ) ;
//if(inputHit==0)continue;
      // now we have to understand which layer this hit belongs to.
//obs.      int sensorID = guessSensorID( inputHit );
      const double * pos = inputHit->getPosition();
      int sensorID = guessSensorID(pos);

      //find proper alignment colleciton:
            double alpha = 0.;
            double beta  = 0.;
            double gamma = 0.;
            double offsetX = 0.;
            double offsetY = 0.;
            double offsetZ = 0.;
 
      // now that we know at which sensor the hit belongs to, we can
      // get the corresponding alignment constants
      map< int , int >::iterator  positionIter = _lookUpTable[ _alignmentCollectionName ].find( sensorID );

     streamlog_out( DEBUG ) << "DIRECT:-----:-----: iHit [" <<  iHit << "] for sensor  "<<  sensorID << endl;
     if ( positionIter == _lookUpTable[ _alignmentCollectionName ].end() )
         {
          streamlog_out( DEBUG ) << "DIRECT:-----:-----: wrong sensorID : " <<  sensorID << " ?? " << endl;
//          continue; //do nothing as if alignment == 0.
         }
      else
         {
           EUTelAlignmentConstant * alignment = static_cast< EUTelAlignmentConstant * >  ( _alignmentCollectionVec->getElementAt( positionIter->second ) );
           streamlog_out( DEBUG ) << "DIRECT:-----:-----: iHit [" <<  iHit << "] at "<<  positionIter->second << " found at (EUTelAlignmentConstant*) " << alignment ;
           streamlog_out( DEBUG ) << " with alignment collection name : " << _alignmentCollectionName << endl;
           alpha   = alignment->getAlpha();
           beta    = alignment->getBeta();
           gamma   = alignment->getGamma();
           offsetX = alignment->getXOffset();
           offsetY = alignment->getYOffset();
           offsetZ = alignment->getZOffset();
         }  


      // refhit = center-of-the-sensor coordinates:
      double x_refhit = 0.; 
      double y_refhit = 0.; 
      double z_refhit = 0.; 
      
      if( _referenceHitVec == 0 )
      {
	// todo: is this case (no reference vector) treated correctly?
        streamlog_out( DEBUG ) << "DIRECT:-----:-----: _referenceHitVec is empty" << endl;
      }
      else
      {
        streamlog_out( DEBUG ) << "DIRECT:-----:-----: reference Hit collection name : " << _referenceHitCollectionName << endl;
 
       for(size_t ii = 0 ; ii <  (unsigned int)_referenceHitVec->getNumberOfElements(); ii++)
       {
        EUTelReferenceHit * refhit = static_cast< EUTelReferenceHit*> ( _referenceHitVec->getElementAt(ii) ) ;
        if( sensorID != refhit->getSensorID() )
        {
//          streamlog_out( DEBUG ) << "DIRECT:-----:-----: Looping through a varity of sensor IDs" << endl;
          continue;
        }
        else
        {
          streamlog_out( DEBUG ) << "DIRECT:-----:-----: Sensor ID and Alignment plane ID match!" << endl;
          x_refhit =  refhit->getXOffset();
          y_refhit =  refhit->getYOffset();
          z_refhit =  refhit->getZOffset();

          x_refhit += offsetX;
          y_refhit += offsetY;
          z_refhit += offsetZ;

          break;
        } 
           streamlog_out( DEBUG ) << "DIRECT:-----:-----: sensorID "<<
             refhit->getSensorID(   )  << " X: "   <<                
             refhit->getXOffset(    )  << " Y: "   <<
             refhit->getYOffset(    )  << " Z: "   <<
             refhit->getZOffset(    )  << " X_REFHIT: "   <<
             x_refhit << " Y_REFHIT: "   <<
             y_refhit << " Z_REFHIT: "   <<
             z_refhit << "  "   << endl;        
       }
      }
      streamlog_out( DEBUG ) << "DIRECT:-----:-----: refhit found for sensorID " << sensorID << endl;

      // copy the input to the output, at least for the common part
      TrackerHitImpl   * outputHit  = new TrackerHitImpl;
      outputHit->setType( inputHit->getType() );
      outputHit->rawHits() = inputHit->getRawHits();

      // hit coordinates in the center-of-the sensor frame (axis coincide with the global frame)
      const double *inputS = static_cast<const double*> ( inputHit->getPosition() ) ;

      double inputPosition[3]      = { inputS[0], inputS[1], inputS[2] };
      double inputPosition_orig[3] = { inputS[0], inputS[1], inputS[2] };

      inputPosition[0] = inputPosition[0] - x_refhit;
      inputPosition[1] = inputPosition[1] - y_refhit;
      inputPosition[2] = inputPosition[2] - z_refhit;

      // initial setting of the sensor = center-of-the sensor coordinates in the global frame
      double   outputPosition[3]  = { 0., 0., 0. };
      outputPosition[0] = x_refhit;
      outputPosition[1] = y_refhit;
      outputPosition[2] = z_refhit;
/*
        printf("PRE sensorID: %5d   [%5.2f %5.2f %5.2f]\n",
                sensorID, x_refhit, y_refhit, z_refhit
        );
*/


//      if ( positionIter != _lookUpTable[ _alignmentCollectionName ].end() ) {


#if ( defined(USE_AIDA) || defined(MARLIN_USE_AIDA) )
        string tempHistoName;
        AIDA::IHistogram3D *histo3D; 
        if ( _histogramSwitch ) {
          {
            stringstream ss;
            ss  << _hitHistoBeforeAlignName << "_" << sensorID ;
            tempHistoName = ss.str();
          }
          if ( AIDA::IHistogram2D * histo = dynamic_cast<AIDA::IHistogram2D*> ( _aidaHistoMap[ tempHistoName ] )) {
            histo->fill( inputPosition[0], inputPosition[1] );
          }
          else
            {
              streamlog_out ( ERROR1 )  << "DIRECT:-----:-----: Not able to retrieve histogram pointer for " << tempHistoName
                                        << ".\nDisabling histogramming from now on " << endl;
              _histogramSwitch = false;
            }
          histo3D = dynamic_cast<AIDA::IHistogram3D*> (_aidaHistoMap[ _densityPlotBeforeAlignName ] );
          if ( histo3D ) histo3D->fill( inputPosition[0], inputPosition[1], inputPosition[2] );
          else {
            streamlog_out ( ERROR1 )  << "DIRECT:-----:-----: Not able to retrieve histogram pointer for " << _densityPlotBeforeAlignName
                                      << ".\nDisabling histogramming from now on " << endl;
            _histogramSwitch = false;
          }
        }
#endif

        
        if ( _correctionMethod == 0 ) 
        {

            // this is the shift only case
            outputPosition[0] += inputPosition[0] - offsetX;                
            outputPosition[1] += inputPosition[1] - offsetY; 
            outputPosition[2] += inputPosition[2] - offsetZ;

        } 
        else if ( _correctionMethod == 1 ) 
        {
            if ( _debugSwitch )
            {
                alpha = _alpha;
                beta  = _beta;
                gamma = _gamma; 
                offsetX = 0.;
                offsetY = 0.;
                offsetZ = 0.;
            }
            else
            {
            }

            if( _iEvt < _printEvents )
            {
                if ( _debugSwitch ) 
                {
                    streamlog_out (  DEBUG  )  << "DIRECT:-----:-----: Debugmode ON " << endl;                                   
                }
                
                streamlog_out (  DEBUG  )  << "DIRECT:-----:-----: _correctionMethod == rotation first " << endl;
                streamlog_out (  DEBUG  )  << "DIRECT:-----:-----:  alignment->getAlpha() = " << alpha  << endl;
                streamlog_out (  DEBUG  )  << "DIRECT:-----:-----:  alignment->getBeta()  = " <<  beta  << endl;
                streamlog_out (  DEBUG  )  << "DIRECT:-----:-----:  alignment->getGamma() = " << gamma << endl;
                streamlog_out (  DEBUG  )  << "DIRECT:-----:-----:  alignment->getXOffest() = " << offsetX  << endl;
                streamlog_out (  DEBUG  )  << "DIRECT:-----:-----:  alignment->getYOffest() = " << offsetY  << endl;
                streamlog_out (  DEBUG  )  << "DIRECT:-----:-----:  alignment->getZOffest() = " << offsetZ  << endl;
            }
        
            // this is the rotation first
            TVector3 iCenterOfSensorFrame(  inputPosition[0],  inputPosition[1],  inputPosition[2] );

            iCenterOfSensorFrame.RotateX( -alpha );
            iCenterOfSensorFrame.RotateY( -beta  );
            iCenterOfSensorFrame.RotateZ( -gamma );

            outputPosition[0] += iCenterOfSensorFrame(0);
            outputPosition[1] += iCenterOfSensorFrame(1);
            outputPosition[2] += iCenterOfSensorFrame(2);

            // second the shift
            outputPosition[0] -= offsetX; 
            outputPosition[1] -= offsetY; 
            outputPosition[2] -= offsetZ; 
          
        }
        else if ( _correctionMethod == 2 ) 
        {
        }

#if ( defined(USE_AIDA) || defined(MARLIN_USE_AIDA) ) 
        if ( _histogramSwitch ) {
          {
            stringstream ss;
            ss  << _hitHistoAfterAlignName << "_" << sensorID ;
            tempHistoName = ss.str();
          }
          if ( AIDA::IHistogram2D * histo = dynamic_cast<AIDA::IHistogram2D*> ( _aidaHistoMap[ tempHistoName ] )) {
            histo->fill( outputPosition[0], outputPosition[1] );
          }
          else {
            streamlog_out ( ERROR1 )  << "DIRECT:-----:-----: Not able to retrieve histogram pointer for " << tempHistoName
                                      << ".\nDisabling histogramming from now on " << endl;
            _histogramSwitch = false;
          }
          histo3D = dynamic_cast<AIDA::IHistogram3D*> (_aidaHistoMap[ _densityPlotAfterAlignName ] );
          if ( histo3D ) histo3D->fill( outputPosition[0], outputPosition[1], outputPosition[2] );
          else {
            streamlog_out ( ERROR1 )  << "DIRECT:-----:-----: Not able to retrieve histogram pointer for " << _densityPlotAfterAlignName
                                      << ".\nDisabling histogramming from now on " << endl;
            _histogramSwitch = false;
          }
        }
#endif

/*
      } 
      else 
      {

        // this hit belongs to a plane whose sensorID is not in the
        // alignment constants. So the idea is to eventually advice
        // the users if running in DEBUG and copy the not aligned hit
        // in the new collection.
//        streamlog_out ( DEBUG ) << "Sensor ID " << sensorID << " not found. Skipping alignment for hit "
//                                << iHit << endl;

        for ( size_t i = 0; i < 3; ++i )
        {
            outputPosition[i] = inputPosition[i];
        }

      }
*/

      if ( _iEvt < _printEvents )
      {
         streamlog_out ( MESSAGE ) << "DIRECT:-----:-----:" << _alignmentCollectionName.c_str() << " : ORIGI: Sensor ID " << sensorID << " " << inputPosition_orig[0] << " " << inputPosition_orig[1] << " " << inputPosition_orig[2] <<  endl;   
         streamlog_out ( MESSAGE ) << "DIRECT:-----:-----:" << _alignmentCollectionName.c_str() << " : INPUT: Sensor ID " << sensorID << " " << inputPosition[0] << " " << inputPosition[1] << " " << inputPosition[2] <<  endl;   
         streamlog_out ( MESSAGE ) << "DIRECT:-----:-----:" << _alignmentCollectionName.c_str() << " : OUTPUT:Sensor ID " << sensorID << " " << outputPosition[0] << " " << outputPosition[1] << " " << outputPosition[2]  << endl;
      }

      outputHit->setPosition( outputPosition ) ;
      _outputCollectionVec->push_back( outputHit );
    }
}

void EUTelApplyAlignmentProcessor::Reverse(LCEvent *event) {
 
    if ( _iEvt % 1000 == 0 )
        streamlog_out ( MESSAGE4 ) << "Processing event (ApplyAlignment Reverse) "
                               << setw(6) << setiosflags(ios::right) << event->getEventNumber() << " in run "
                               << setw(6) << setiosflags(ios::right) << setfill('0')  << event->getRunNumber()
                               << setfill(' ')
                               << " (Total = " << setw(10) << _iEvt << ")" << resetiosflags(ios::left) << endl;
//    ++_iEvt;


    EUTelEventImpl * evt = static_cast<EUTelEventImpl*> (event);

    if ( evt->getEventType() == kEORE ) 
    {
        streamlog_out ( DEBUG4 ) << "EORE found: nothing else to do." << endl;
        return;
    }
    else if ( evt->getEventType() == kUNKNOWN ) 
    {
        streamlog_out ( WARNING2 ) << "Event number " << evt->getEventNumber() << " in run " << evt->getRunNumber()
            << " is of unknown type. Continue considering it as a normal Data Event." << endl;
    }

  // ----------------------------------------------------------------------- //
  // check input / output collections

  // CheckIOCollections(event);

  //
  // ----------------------------------------------------------------------- //


        if (_fevent) 
        {
            streamlog_out ( MESSAGE ) << "REVERSE: The alignment collection ["<< _alignmentCollectionName.c_str() <<"] contains: " <<  _alignmentCollectionVec << endl ; 
            streamlog_out ( MESSAGE ) << "REVERSE: The alignment collection ["<< _alignmentCollectionName.c_str() <<"] contains: " <<  _alignmentCollectionVec->size() << " planes " << endl;    

            if(_alignmentCollectionVec->size() > 0 )
            {
                streamlog_out ( MESSAGE ) << "REVERSE: alignment sensorID: " ;
                for ( size_t iPos = 0; iPos < _alignmentCollectionVec->size(); ++iPos ) 
                {
                    EUTelAlignmentConstant * alignment = static_cast< EUTelAlignmentConstant * > ( _alignmentCollectionVec->getElementAt( iPos ) );
                    _lookUpTable[ _alignmentCollectionName ][ alignment->getSensorID() ] = iPos;
                    streamlog_out ( MESSAGE ) << " _alignmentCollectionVec element position : " << iPos << " " << " align for " << alignment->getSensorID() << endl ;

                    if ( _applyToReferenceHitCollection ) 
                    {
                     AlignReferenceHit( evt,  alignment); 
                    }
                } 
                streamlog_out ( MESSAGE ) << endl;
            }
            
//            if ( _histogramSwitch ) 
//            {
//                crashes if commented out, why ??
//                bookHistos();
//            }
            
#ifndef NDEBUG
#endif
        }

        streamlog_out ( DEBUG ) << "REVERSE: The alignment collection ["<< _alignmentCollectionName.c_str() <<"] contains: " <<  _alignmentCollectionVec->size() << " planes " << endl;   
        streamlog_out ( DEBUG ) << "REVERSE: _lookUpTable contains "<< _lookUpTable.size() << " elements " << endl;

       // print out the lookup table
        map< int , int >::iterator mapIter = _lookUpTable[ _alignmentCollectionName ].begin();
        while ( mapIter != _lookUpTable[ _alignmentCollectionName ].end() ) 
          {
                streamlog_out ( DEBUG ) << "REVERSE: Sensor ID = " << mapIter->first
                                        << " is in position " << mapIter->second << endl;
                ++mapIter;
          }

// final check
    if( _inputCollectionVec == 0 )
    {
      streamlog_out ( DEBUG ) << "REVERSE: EUTelApplyAlignmentProcessor::Reverse. Skip this event. Input Collection not found. " << endl;  
      return;
    }

// go-go
        for (size_t iHit = 0; iHit < _inputCollectionVec->size(); iHit++) 
        {

            TrackerHitImpl   * inputHit   = dynamic_cast< TrackerHitImpl * >  ( _inputCollectionVec->getElementAt( iHit ) ) ;

            // now we have to understand which layer this hit belongs to.
//            int sensorID = guessSensorID( inputHit );
            const double * pos = inputHit->getPosition();
            int sensorID = guessSensorID(pos);

            // determine z position of the plane
            // 20 December 2010 @libov

/*            float	z_sensor = 0;
            for ( int iPlane = 0 ; iPlane < _siPlanesLayerLayout->getNLayers(); ++iPlane ) 
            {
                if (sensorID == _siPlanesLayerLayout->getID( iPlane ) ) 
                {
                    z_sensor = _siPlanesLayerLayout -> getSensitivePositionZ( iPlane ) + 0.5 * _siPlanesLayerLayout->getSensitiveThickness( iPlane );
                    break;
                }
            }
*/
      //find proper alignment colleciton:
      double alpha = 0.;
      double beta  = 0.;
      double gamma = 0.;
      double offsetX = 0.;
      double offsetY = 0.;
      double offsetZ = 0.;
 
      // now that we know at which sensor the hit belongs to, we can
      // get the corresponding alignment constants
      map< int , int >::iterator  positionIter = _lookUpTable[ _alignmentCollectionName ].find( sensorID );

//      printf(" positionIter %5d at %5d \n", sensorID, positionIter->second );
      if ( positionIter == _lookUpTable[ _alignmentCollectionName ].end() )
         {
           streamlog_out( DEBUG ) <<  "REVERSE: wrong sensorId " << sensorID  << endl;
//           continue; do nothing as if alignment == 0.
         }
      else
         {
           streamlog_out( DEBUG ) << "REVERSE: FOUND alignment collection "<< _alignmentCollectionName.c_str() <<" record for sensorId " << sensorID  << endl;
           EUTelAlignmentConstant * alignment = static_cast< EUTelAlignmentConstant * >  ( _alignmentCollectionVec->getElementAt( positionIter->second ) );

           alpha   = alignment->getAlpha();
           beta    = alignment->getBeta();
           gamma   = alignment->getGamma();
           offsetX = alignment->getXOffset();
           offsetY = alignment->getYOffset();
           offsetZ = alignment->getZOffset();
         }  


      // refhit = center-of-the-sensor coordinates:
      double x_refhit = 0.; 
      double y_refhit = 0.; 
      double z_refhit = 0.; 

      if( _referenceHitVec == 0 )
      {
	// todo: is this case (no reference vector) treated correctly?
        // streamlog_out(MESSAGE) << "_referenceHitVec is empty" << endl;
      }
      else
      {
        if(_fevent) streamlog_out(MESSAGE) << "REVERSE: reference Hit collection name : " << _referenceHitCollectionName << " at " << _referenceHitVec  << endl;

        for(size_t ii = 0 ; ii <  (unsigned int)_referenceHitVec->getNumberOfElements(); ii++)
        {
          EUTelReferenceHit * refhit = static_cast< EUTelReferenceHit*> ( _referenceHitVec->getElementAt(ii) ) ;
          if( sensorID != refhit->getSensorID() )
          {
            // streamlog_out(MESSAGE) << "Looping through a varity of sensor IDs" << endl;
            continue;
          }
          else
          {
//           streamlog_out(MESSAGE) << "Sensor ID and Alignment plane ID match!" << endl;
            x_refhit =  refhit->getXOffset();
            y_refhit =  refhit->getYOffset();
            z_refhit =  refhit->getZOffset();

// do not apply this part: refhits should be just as they where befre the alignment has been applied
// = it means the anti-apply alignment should have been applied already in the AlignReferenceHit
//---//
//          x_refhit -= offsetX;
//          y_refhit -= offsetY;
//          z_refhit -= offsetZ;
//---//

            break;
          } 
/*
        printf("PRE sensorID: %5d dx:%5.3f dy:%5.3f dz:%5.3f  [%5.2f %5.2f %5.2f]\n",
        refhit->getSensorID(   ),                      
        refhit->getXOffset(    ),
        refhit->getYOffset(    ),
        refhit->getZOffset(    ), x_refhit, y_refhit, z_refhit
        );
*/
        }
      }
 
      // copy the input to the output, at least for the common part
      TrackerHitImpl   * outputHit  = new TrackerHitImpl;
      outputHit->setType( inputHit->getType() );
      outputHit->rawHits() = inputHit->getRawHits();

      // now that we know at which sensor the hit belongs to, we can
      // get the corresponding alignment constants
      // map< int , int >::iterator  positionIter = _lookUpTable[ _alignmentCollectionName ].find( sensorID );

      // hit coordinates in the center-of-the sensor frame (axis coincide with the global frame)
      const double *inputS = static_cast<const double*> ( inputHit->getPosition() ) ;

      double inputPosition[3]      = { inputS[0], inputS[1], inputS[2] };
      double inputPosition_orig[3] = { inputS[0], inputS[1], inputS[2] };

      inputPosition[0] = inputPosition[0] - x_refhit;
      inputPosition[1] = inputPosition[1] - y_refhit;
      inputPosition[2] = inputPosition[2] - z_refhit;


//            const double * inputPosition = const_cast< const double * > ( inputHit->getPosition() ) ;
//            double   outputPosition[3]   = { 0., 0., 0. };
  
      // initial setting of the sensor = center-of-the sensor coordinates in the global frame
      double   outputPosition[3]  = { 0., 0., 0. };
      outputPosition[0] = x_refhit;
      outputPosition[1] = y_refhit;
      outputPosition[2] = z_refhit;

//            if ( positionIter != _lookUpTable[ _alignmentCollectionName ].end() ) 
//            {

#if ( defined(USE_AIDA) || defined(MARLIN_USE_AIDA) )
                string tempHistoName;
                AIDA::IHistogram3D *histo3D; 
                if ( _histogramSwitch ) 
                {
                    {
                        stringstream ss;
                        ss  << _hitHistoBeforeAlignName << "_" << sensorID ;
                        tempHistoName = ss.str();
                    }
                    if ( AIDA::IHistogram2D * histo = dynamic_cast<AIDA::IHistogram2D*> ( _aidaHistoMap[ tempHistoName ] )) 
                    {
                        histo->fill( inputPosition[0], inputPosition[1] );
                    }
                    else
                    {
                        streamlog_out ( ERROR1 )  << "Not able to retrieve histogram pointer for " << tempHistoName
                                                  << ".\nDisabling histogramming from now on " << endl;
                        _histogramSwitch = false;
                    }
                    
                    histo3D = dynamic_cast<AIDA::IHistogram3D*> (_aidaHistoMap[ _densityPlotBeforeAlignName ] );
                    if ( histo3D ) 
                    {
                        histo3D->fill( inputPosition[0], inputPosition[1], inputPosition[2] );
                    }
                    else 
                    {
                        streamlog_out ( ERROR1 )  << "Not able to retrieve histogram pointer for " << _densityPlotBeforeAlignName
                                                  << ".\nDisabling histogramming from now on " << endl;
                        _histogramSwitch = false;
                    }
                }
#endif

       
                if ( _correctionMethod == 0 ) 
                {                   

                    // this is the shift only case

                    outputPosition[0] = inputPosition[0] + offsetX ;
                    outputPosition[1] = inputPosition[1] + offsetY ;
                    outputPosition[2] = inputPosition[2] + offsetZ ;

                }
                else
                    if ( _correctionMethod == 1 ) 
                    {
                        // this is the rotation first
                        if ( _debugSwitch )
                        {
                          alpha = _alpha;
                          beta  = _beta;
                          gamma = _gamma; 
                          offsetX = 0.;
                          offsetY = 0.;
                          offsetZ = 0.;
                        }
                        else
                        {
                        }

            if( _iEvt < _printEvents )
            {
                if ( _debugSwitch ) 
                {
                    streamlog_out ( MESSAGE )  << "Debugmode ON " << endl;                                   
                }
                
                streamlog_out ( MESSAGE )  << "_correctionMethod == rotation first " << endl;
                streamlog_out ( MESSAGE )  << " alignment->getAlpha() = " << alpha  << endl;
                streamlog_out ( MESSAGE )  << " alignment->getBeta()  = " <<  beta  << endl;
                streamlog_out ( MESSAGE )  << " alignment->getGamma() = " << gamma << endl;
                streamlog_out ( MESSAGE )  << " alignment->getXOffest() = " << offsetX  << endl;
                streamlog_out ( MESSAGE )  << " alignment->getYOffest() = " << offsetY  << endl;
                streamlog_out ( MESSAGE )  << " alignment->getZOffest() = " << offsetZ  << endl;
            }
        
            // this is the rotation first
            TVector3 iCenterOfSensorFrame(  inputPosition[0],  inputPosition[1],  inputPosition[2] );

            iCenterOfSensorFrame.RotateZ( +gamma );
            iCenterOfSensorFrame.RotateY( +beta  );
            iCenterOfSensorFrame.RotateX( +alpha );

            outputPosition[0] += iCenterOfSensorFrame(0);
            outputPosition[1] += iCenterOfSensorFrame(1);
            outputPosition[2] += iCenterOfSensorFrame(2);

            // second the shift
            outputPosition[0] += offsetX; 
            outputPosition[1] += offsetY; 
            outputPosition[2] += offsetZ; 

                    }
                    else
                        if ( _correctionMethod == 2 ) 
                        {
                        }

#if ( defined(USE_AIDA) || defined(MARLIN_USE_AIDA) ) 

                if ( _histogramSwitch ) 
                {
                    {
                        stringstream ss;
                        ss  << _hitHistoAfterAlignName << "_" << sensorID ;
                        tempHistoName = ss.str();
                    }
                    if ( AIDA::IHistogram2D * histo = dynamic_cast<AIDA::IHistogram2D*> ( _aidaHistoMap[ tempHistoName ] )) 
                    {
                        histo->fill( outputPosition[0], outputPosition[1] );
                    }
                    else 
                    {
                        streamlog_out ( ERROR1 )  << "Not able to retrieve histogram pointer for " << tempHistoName
                                                  << ".\nDisabling histogramming from now on " << endl;
                        _histogramSwitch = false;
                    }
                    
                    histo3D = dynamic_cast<AIDA::IHistogram3D*> (_aidaHistoMap[ _densityPlotAfterAlignName ] );
                    if ( histo3D )
                    {
                        histo3D->fill( outputPosition[0], outputPosition[1], outputPosition[2] );
                    }
                    else 
                    {
                        streamlog_out ( ERROR1 )  << "Not able to retrieve histogram pointer for " << _densityPlotAfterAlignName
                                                  << ".\nDisabling histogramming from now on " << endl;
                        _histogramSwitch = false;
                    }
                }
#endif
/*
            }
            else 
            {

                // this hit belongs to a plane whose sensorID is not in the
                // alignment constants. So the idea is to eventually advice
                // the users if running in DEBUG and copy the not aligned hit
                // in the new collection.

                streamlog_out ( DEBUG   ) << "Sensor ID " << sensorID << " not found. Skipping alignment for hit "
                                          << iHit << endl;
                
                for ( size_t i = 0; i < 3; ++i )
                {
                    outputPosition[i] = inputPosition[i];
                }
            }
*/
      if ( _iEvt < _printEvents )
      {
         streamlog_out ( MESSAGE ) <<_alignmentCollectionName.c_str() << " REVERT: ORIGI: Sensor ID " << sensorID << " " << inputPosition_orig[0] << " " << inputPosition_orig[1] << " " << inputPosition_orig[2] <<  endl;   
         streamlog_out ( MESSAGE ) <<_alignmentCollectionName.c_str() << " REVERT: INPUT: Sensor ID " << sensorID << " " << inputPosition[0] << " " << inputPosition[1] << " " << inputPosition[2] <<  endl;   
         streamlog_out ( MESSAGE ) <<_alignmentCollectionName.c_str() << " REVERT: OUTPUT:Sensor ID " << sensorID << " " << outputPosition[0] << " " << outputPosition[1] << " " << outputPosition[2]  << endl;
      }

            outputHit->setPosition( outputPosition ) ;
            _outputCollectionVec->push_back( outputHit );
        }

    
}

void EUTelApplyAlignmentProcessor::bookHistos() {

#if ( defined(USE_AIDA) || defined(MARLIN_USE_AIDA) )

  try {
    streamlog_out ( MESSAGE4 ) <<  "Booking histograms" << endl;

    string tempHistoName;

    // histograms are grouped into folders named after the
    // detector. This requires to loop on detector now.
    for (int iDet = 0 ; iDet < _siPlanesParameters->getSiPlanesNumber(); iDet++) 
    {
      int sensorID = _siPlanesLayerLayout->getID( iDet ) ;
 
      streamlog_out ( MESSAGE4 ) <<  "Booking histograms for sensorID: " << sensorID << endl;

      string basePath;
      {
        stringstream ss ;
        ss << "plane-" << sensorID;
        basePath = ss.str();
      }
      AIDAProcessor::tree(this)->mkdir(basePath.c_str());
      basePath = basePath + "/";

      // 2 should be enough because it
      // means that the sensor is wrong
      // by all its size.
      double safetyFactor = 1.0;

      double xMin = safetyFactor * ( _siPlanesLayerLayout->getSensitivePositionX( iDet ) -
                                     ( 0.5 * _siPlanesLayerLayout->getSensitiveSizeX ( iDet ) ));
      double xMax = safetyFactor * ( _siPlanesLayerLayout->getSensitivePositionX( iDet ) +
                                     ( 0.5 * _siPlanesLayerLayout->getSensitiveSizeX ( iDet )));

      double yMin = safetyFactor * ( _siPlanesLayerLayout->getSensitivePositionY( iDet ) -
                                     ( 0.5 * _siPlanesLayerLayout->getSensitiveSizeY ( iDet )));
      double yMax = safetyFactor * ( _siPlanesLayerLayout->getSensitivePositionY( iDet ) +
                                     ( 0.5 * _siPlanesLayerLayout->getSensitiveSizeY ( iDet )) );

      int xNBin = static_cast< int > ( safetyFactor ) * _siPlanesLayerLayout->getSensitiveNpixelX( iDet );
      int yNBin = static_cast< int > ( safetyFactor ) * _siPlanesLayerLayout->getSensitiveNpixelY( iDet );

      {
        stringstream ss ;
        ss <<  _hitHistoBeforeAlignName << "_" << sensorID ;
        tempHistoName = ss.str();
      }
      AIDA::IHistogram2D * hitHistoBeforeAlign =
        AIDAProcessor::histogramFactory(this)->createHistogram2D( ( basePath + tempHistoName ).c_str(),
                                                                  xNBin, xMin, xMax, yNBin, yMin, yMax );

      if ( hitHistoBeforeAlign ) {
        hitHistoBeforeAlign->setTitle("Hit map in the telescope frame of reference before align");
        _aidaHistoMap.insert( make_pair ( tempHistoName, hitHistoBeforeAlign ) );
      } else {
        streamlog_out ( ERROR1 )  << "Problem booking the " << (basePath + tempHistoName) << ".\n"
                                  << "Very likely a problem with path name. Switching off histogramming and continue w/o" << endl;
        _histogramSwitch = false;
      }


      {
        stringstream ss ;
        ss <<  _hitHistoAfterAlignName << "_" << sensorID  ;
        tempHistoName = ss.str();
      }
      AIDA::IHistogram2D * hitHistoAfterAlign =
        AIDAProcessor::histogramFactory(this)->createHistogram2D( ( basePath + tempHistoName ).c_str(),
                                                                  xNBin, xMin, xMax, yNBin, yMin, yMax );

      if ( hitHistoAfterAlign ) {
        hitHistoAfterAlign->setTitle("Hit map in the telescope frame of reference after align");
        _aidaHistoMap.insert( make_pair ( tempHistoName, hitHistoAfterAlign ) );
      } else {
        streamlog_out ( ERROR1 )  << "Problem booking the " << (basePath + tempHistoName) << ".\n"
                                  << "Very likely a problem with path name. Switching off histogramming and continue w/o" << endl;
        _histogramSwitch = false;
      }
    }

    // we have to found the boundaries of this histograms. Let's take
    // the outer positions in all directions
    double xMin  =      numeric_limits< double >::max();
    double xMax  = -1 * numeric_limits< double >::max();
    int    xNBin = numeric_limits< int >::min();

    double yMin  =      numeric_limits< double >::max();
    double yMax  = -1 * numeric_limits< double >::max();
    int    yNBin = numeric_limits< int >::min();

    for ( int iPlane = 0 ; iPlane < _siPlanesParameters->getSiPlanesNumber(); ++iPlane ) {

      // x axis
      xMin  = min( _siPlanesLayerLayout->getSensitivePositionX( iPlane ) - ( 0.5  * _siPlanesLayerLayout->getSensitiveSizeX( iPlane )), xMin);
      xMax  = max( _siPlanesLayerLayout->getSensitivePositionX( iPlane ) + ( 0.5  * _siPlanesLayerLayout->getSensitiveSizeX( iPlane )), xMax);
      xNBin = max( _siPlanesLayerLayout->getSensitiveNpixelX( iPlane ), xNBin );

      // y axis
      yMin  = min( _siPlanesLayerLayout->getSensitivePositionY( iPlane ) - ( 0.5  * _siPlanesLayerLayout->getSensitiveSizeY( iPlane )), yMin);
      yMax  = max( _siPlanesLayerLayout->getSensitivePositionY( iPlane ) + ( 0.5  * _siPlanesLayerLayout->getSensitiveSizeY( iPlane )), yMax);
      yNBin = max( _siPlanesLayerLayout->getSensitiveNpixelY( iPlane ), yNBin );

    }


    // since we may still have alignment problem, we have to take a
    // safety factor on the x and y direction especially.
    // here I take something less than 2 because otherwise I will have
    // a 200MB histogram.
    double safetyFactor = 1.0;

    double xDistance = std::abs( xMax - xMin ) ;
    double xCenter   = ( xMax + xMin ) / 2 ;
    xMin  = xCenter - safetyFactor * ( xDistance / 2 );
    xMax  = xCenter + safetyFactor * ( xDistance / 2 );
    xNBin = static_cast< int > ( xNBin * safetyFactor );

    // generate the x axis binning
    vector< double > xAxis;
    double step = xDistance / xNBin;
    for ( int i = 0 ; i < xNBin ; ++i ) {
      xAxis.push_back ( xMin + i * step );
    }

    double yDistance = std::abs( yMax - yMin ) ;
    double yCenter   = ( yMax + yMin ) / 2 ;
    yMin  = yCenter - safetyFactor * ( yDistance / 2 );
    yMax  = yCenter + safetyFactor * ( yDistance / 2 );
    yNBin = static_cast< int > ( yNBin * safetyFactor );

    // generate the y axis binning
    vector< double > yAxis;
    step = yDistance / yNBin;
    for ( int i = 0 ; i < yNBin ; ++i ) {
      yAxis.push_back( yMin + i * step ) ;
    }


    // generate the z axis but not equally spaced!
    double safetyMargin = 10; // this is mm
    vector< double > zAxis;
    for ( int i = 0 ; i < 2 * _siPlanesParameters->getSiPlanesNumber(); ++i ) {
      double zPos =  _siPlanesLayerLayout->getSensitivePositionZ( i/2 );
      zAxis.push_back( zPos - safetyMargin) ;
      ++i;
      zAxis.push_back( zPos + safetyMargin );
    }


    AIDA::IHistogram3D * densityPlot = AIDAProcessor::histogramFactory(this)->createHistogram3D( _densityPlotBeforeAlignName ,
                                                                                                 "Hit position in the telescope frame of reference before align",
                                                                                                 xAxis, yAxis, zAxis, "");

    if ( densityPlot ) {
      _aidaHistoMap.insert( make_pair ( _densityPlotBeforeAlignName, densityPlot ) ) ;
    } else {
      streamlog_out ( ERROR1 )  << "Problem booking the " << (_densityPlotBeforeAlignName) << ".\n"
                                << "Very likely a problem with path name. Switching off histogramming and continue w/o" << endl;
      _histogramSwitch = false;
    }


    densityPlot = AIDAProcessor::histogramFactory(this)->createHistogram3D( _densityPlotAfterAlignName ,
                                                                            "Hit position in the telescope frame of reference after align",
                                                                            xAxis, yAxis, zAxis, "");

    if ( densityPlot ) {
      _aidaHistoMap.insert( make_pair ( _densityPlotAfterAlignName, densityPlot ) ) ;
    } else {
      streamlog_out ( ERROR1 )  << "Problem booking the " << (_densityPlotAfterAlignName) << ".\n"
                                << "Very likely a problem with path name. Switching off histogramming and continue w/o" << endl;
      _histogramSwitch = false;
    }

    streamlog_out ( MESSAGE4 ) <<  "Booking histograms DONE" << endl;

  } catch (lcio::Exception& e ) {

    streamlog_out ( ERROR1 ) << "No AIDAProcessor initialized. Type q to exit or c to continue without histogramming" << endl;
    string answer;
    while ( true ) {
      streamlog_out ( ERROR1 ) <<  "[q]/[c]" << endl;
      cin >> answer;
      transform( answer.begin(), answer.end(), answer.begin(), ::tolower );
      if ( answer == "q" ) {
        exit(-1);
      } else if ( answer == "c" )
        _histogramSwitch = false;
      break;
    }
  }
#endif // AIDA && GEAR
}


void EUTelApplyAlignmentProcessor::check (LCEvent * /* evt */ ) {
  // nothing to check here - could be used to fill check plots in reconstruction processor
}


void EUTelApplyAlignmentProcessor::end() {
  streamlog_out ( MESSAGE2 ) <<  "Successfully finished" << endl;
 

  delete [] _siPlaneZPosition;

}

int EUTelApplyAlignmentProcessor::guessSensorID( TrackerHitImpl * hit ) {

  int sensorID = -1;
  double minDistance =  numeric_limits< double >::max() ;



  if( _referenceHitVec == 0 || _applyToReferenceHitCollection == false )
    {
      // use z information of planes instead of reference vector

      double * hitPosition = const_cast<double * > (hit->getPosition());

      for ( int iPlane = 0 ; iPlane < _siPlanesLayerLayout->getNLayers(); ++iPlane ) 
	{
	  double distance = std::abs( hitPosition[2] - _siPlaneZPosition[ iPlane ] );
	  if ( distance < minDistance ) 
	    {
	      minDistance = distance;
	      sensorID = _siPlanesLayerLayout->getID( iPlane );
	    }
	}
      if  ( _siPlanesParameters->getSiPlanesType() == _siPlanesParameters->TelescopeWithDUT ) 
	{
	  double distance = std::abs( hitPosition[2] - _siPlanesLayerLayout->getDUTPositionZ() );
	  if( distance < minDistance )
	    {
	      minDistance = distance;
	      sensorID = _siPlanesLayerLayout->getDUTID();
	    }
	}
      if ( minDistance > 10  ) //mm 
	{
	  // advice the user that the guessing wasn't successful 
	  streamlog_out( WARNING3 ) << "A hit was found " << minDistance << " mm far from the nearest plane\n"
	    "Please check the consistency of the data with the GEAR file " << endl;
	  throw SkipEventException(this);
	}

      return sensorID;
    } // if not using ref vec


        try
        {
            LCObjectVec clusterVector = hit->getRawHits();

            EUTelVirtualCluster *cluster = 0;
// printf("cluster type %5d \n", hit->getType() );
            if ( hit->getType() == kEUTelBrickedClusterImpl ) {

               // fixed cluster implementation. Remember it
               //  can come from
               //  both RAW and ZS data
   
               cluster = new EUTelBrickedClusterImpl(static_cast<TrackerDataImpl *> ( clusterVector[0] ) );
                
            } else if ( hit->getType() == kEUTelDFFClusterImpl ) {
              
              // fixed cluster implementation. Remember it can come from
              // both RAW and ZS data
              cluster = new EUTelDFFClusterImpl( static_cast<TrackerDataImpl *> ( clusterVector[0] ) );
            } else if ( hit->getType() == kEUTelFFClusterImpl ) {
              
              // fixed cluster implementation. Remember it can come from
              // both RAW and ZS data
              cluster = new EUTelFFClusterImpl( static_cast<TrackerDataImpl *> ( clusterVector[0] ) );
            } 
            else if ( hit->getType() == kEUTelAPIXClusterImpl ) 
            {
              
//              cluster = new EUTelSparseClusterImpl< EUTelAPIXSparsePixel >
//                 ( static_cast<TrackerDataImpl *> ( clusterVector[ 0 ]  ) );

                // streamlog_out(MESSAGE4) << "Type is kEUTelAPIXClusterImpl" << endl;
                TrackerDataImpl * clusterFrame = static_cast<TrackerDataImpl*> ( clusterVector[0] );
                // streamlog_out(MESSAGE4) << "Vector size is: " << clusterVector.size() << endl;

                cluster = new eutelescope::EUTelSparseClusterImpl< eutelescope::EUTelAPIXSparsePixel >(clusterFrame);
	      
            }
            else if ( hit->getType() == kEUTelSparseClusterImpl ) 
            {
               cluster = new EUTelSparseClusterImpl< EUTelSimpleSparsePixel > ( static_cast<TrackerDataImpl *> ( clusterVector[0] ) );
            }

            if(cluster != 0)
            {
            int sensorID = cluster->getDetectorID();
//            printf("ApplyAlignement :: actual sensorID: %5d \n", sensorID );
            return sensorID;
            }
          }
          catch(...)
          {
            streamlog_out(ERROR4) << "guess SensorID crashed" << endl;
          }
  return sensorID;

}


void EUTelApplyAlignmentProcessor::TransformToLocalFrame(TrackerHitImpl* outputHit, LCEvent * ev) 
{
// revert alignment, in an inverse order... this part should be fixed // perhaps it's obsolete already// Rubinskiy 12.11.2011
//	for ( int i = _alignmentCollectionNames.size() - 1; i >= 0; i--) 
//      {
//          revertAlignment (x, y, z, _alignmentCollectionNames[i], ev );
//	}

        double *outputPosition = const_cast< double * > ( outputHit->getPosition() ) ;

        // now we have to understand which layer this hit belongs to.
        int sensorID = guessSensorID( outputHit );

        if ( _conversionIdMap.size() != (unsigned) _siPlanesParameters->getSiPlanesNumber() ) 
        {
          // first of all try to see if this sensorID already belong to
          if ( _conversionIdMap.find( sensorID ) == _conversionIdMap.end() ) 
          {
              // this means that this detector ID was not already inserted,
              // so this is the right place to do that
          
              for ( int iLayer = 0; iLayer < _siPlanesLayerLayout->getNLayers(); iLayer++ ) 
              {
                  if ( _siPlanesLayerLayout->getID(iLayer) == sensorID ) 
                  {
                      _conversionIdMap.insert( make_pair( sensorID, iLayer ) );
                      break;
                  }
              }
          }
        }
      
        int layerIndex   = _conversionIdMap[sensorID];


        double xSize = 0., ySize = 0.;
        double xPitch = 0., yPitch = 0.;
        double xPointing[2] = { 1., 0. }, yPointing[2] = { 1., 0. };

        xPitch       = _siPlanesLayerLayout->getSensitivePitchX(layerIndex);    // mm
        yPitch       = _siPlanesLayerLayout->getSensitivePitchY(layerIndex);    // mm
        xSize        = _siPlanesLayerLayout->getSensitiveSizeX(layerIndex);     // mm
        ySize        = _siPlanesLayerLayout->getSensitiveSizeY(layerIndex);     // mm
        xPointing[0] = _siPlanesLayerLayout->getSensitiveRotation1(layerIndex); // was -1 ;
        xPointing[1] = _siPlanesLayerLayout->getSensitiveRotation2(layerIndex); // was  0 ;
        yPointing[0] = _siPlanesLayerLayout->getSensitiveRotation3(layerIndex); // was  0 ;
        yPointing[1] = _siPlanesLayerLayout->getSensitiveRotation4(layerIndex); // was -1 ;

        double sign = 0;
          if      ( xPointing[0] < -0.7 )     sign = -1 ;
          else if ( xPointing[0] > 0.7 )      sign =  1 ;
          else 
          {
           if       ( xPointing[1] < -0.7 )   sign = -1 ;
           else if  ( xPointing[1] > 0.7 )    sign =  1 ;
          }
          outputPosition[0] -=  (-1)* sign * xSize/2;   //apply shifts few lines later

          if      ( yPointing[0] < -0.7 )     sign = -1 ;
          else if ( yPointing[0] > 0.7 )      sign =  1 ;
          else 
          {
           if       ( yPointing[1] < -0.7 )   sign = -1 ;
           else if  ( yPointing[1] > 0.7 )    sign =  1 ;
          }
          outputPosition[1] -= (-1)* sign * ySize/2;   // apply shifts few lines later

        TMatrix flip0(2,1);
          flip0(0,0) = outputPosition[0];
          flip0(1,0) = outputPosition[1];

        TMatrix flip1(2,1);
          flip1(0,0) = 0.;
          flip1(1,0) = 0.;

        TMatrix flip(2,2);
          flip(0,0) = xPointing[0];  
          flip(1,0) = yPointing[0];  
          flip(0,1) = xPointing[1];  
          flip(1,1) = yPointing[1];  

        TMatrix antiflip(2,2);
          antiflip = flip.Invert();
         
        flip1.Mult(antiflip, flip0);

        TVector2 clusterCenter( flip1(0,0)/xPitch-0.5, flip1(1,0)/yPitch-0.5 ); 
        if ( _iEvt < _printEvents )
        {
          streamlog_out ( MESSAGE ) << "RevertGear: antiflip: " << antiflip(0,0) << ":" << antiflip(1,0) << endl;
          streamlog_out ( MESSAGE ) << "RevertGear: antiflip: " << antiflip(0,1) << ":" << antiflip(1,1) << endl;
          streamlog_out ( MESSAGE ) << "RevertGear: matrix: " << flip1(0,0) << ":" << flip1(1,0) << endl;
          streamlog_out ( MESSAGE ) << "RevertGear: cluster coordinates [pitch:"<<xPitch<<":"<<yPitch<<"]:" << clusterCenter.X() << " " << clusterCenter.Y() << endl;
        }

        EUTelVirtualCluster  * cluster;
        ClusterType type = static_cast<ClusterType>(static_cast<int>(outputHit->getType()));
        if ( _iEvt < _printEvents )
        {
          streamlog_out ( MESSAGE ) << "RevertGear: hit/cluster type:" << type << endl;
        }

        // prepare a LCObjectVec to store the current cluster
        LCObjectVec clusterVec = outputHit->getRawHits();
        for(size_t i=0; i < clusterVec.size(); i++)
        {      
          if ( _iEvt < _printEvents )
          {
            streamlog_out ( MESSAGE ) << "[" << i << "]" ;
          }
          if ( type == kEUTelSparseClusterImpl ) 
          {
            cluster = new EUTelSparseClusterImpl< EUTelSimpleSparsePixel > ( static_cast<TrackerDataImpl *> ( clusterVec[i]  ) );
          } 
          else if ( type == kEUTelAPIXClusterImpl ) 
          {
        
            cluster = new EUTelSparseClusterImpl< EUTelAPIXSparsePixel > ( static_cast<TrackerDataImpl *> ( clusterVec[i]  ) );
//            printf("ID: %5d:  x:%5d y:%5d tot:%5.1f\n", cluster->getDetectorID(), xsize0, ysize0,  cluster->getTotalCharge() );
          }
          else 
          {
            streamlog_out ( ERROR4 ) <<  "Unknown cluster type. Sorry for quitting" << endl;
            throw UnknownDataTypeException("Cluster type unknown");
          }
          float xCoG(0.0f), yCoG(0.0f);
          cluster->getCenterOfGravity(xCoG, yCoG);
          double xDet = (xCoG + 0.5) * xPitch;
          double yDet = (yCoG + 0.5) * yPitch;

          double telPos[3];
          telPos[0] = xPointing[0] * xDet + xPointing[1] * yDet;
          telPos[1] = yPointing[0] * xDet + yPointing[1] * yDet;

          // now the translation
          // not sure about the sign. At least it is working for the current
          // configuration but we need to double check it
          double sign = 0;
          if      ( xPointing[0] < -0.7 )       sign = -1 ;
          else if ( xPointing[0] > 0.7 )       sign =  1 ;
          else 
          {
           if       ( xPointing[1] < -0.7 )    sign = -1 ;
           else if  ( xPointing[1] > 0.7 )    sign =  1 ;
          }
          telPos[0] +=  (-1)* sign * xSize/2;   //apply shifts few lines later

          if      ( yPointing[0] < -0.7 )       sign = -1 ;
          else if ( yPointing[0] > 0.7 )       sign =  1 ;
          else 
          {
           if       ( yPointing[1] < -0.7 )    sign = -1 ;
           else if  ( yPointing[1] > 0.7 )    sign =  1 ;
          }
          telPos[1] += (-1)* sign * ySize/2;   // apply shifts few lines later

          if ( _iEvt < _printEvents )
          {
            streamlog_out ( MESSAGE ) << "[CoG:" << xCoG << ":" << yCoG << " -> Det:" << xDet << ":" << yDet << "] == " << telPos[0] << ":" << telPos[1] ;
          }
        }
 
        if ( _iEvt < _printEvents )
        { 
          streamlog_out ( MESSAGE ) << endl;
        }

}

void EUTelApplyAlignmentProcessor::revertAlignment(double & x, double & y, double & z, std::string	collectionName, LCEvent * event) 
{

	// in this function, some parts of the EUTelApplyAlignmentProcessor are used


    // ----------------------------------------------------------------------- //
    // check input / output collections

    // CheckIOCollections(event);

    //
    // ----------------------------------------------------------------------- //

    // next, find the alignment constant corresponding to the DUT
	EUTelAlignmentConstant * c = NULL;
    
    int _manualDUTid = 10;

	for ( size_t iPos = 0; iPos < _alignmentCollectionVec->size(); ++iPos ) {

		c = static_cast< EUTelAlignmentConstant * > ( _alignmentCollectionVec->getElementAt( iPos ) );
		if (c -> getSensorID() == _manualDUTid ) break;	// this means we found the alignment constant corresponding
    													// to the DUT; the pointer to it is now stored in c and can
														// be furhter used
	}

	if ( c == NULL ) {
		cout << "Was not possible to found alignment constant, terminating" << endl;
		abort();
	}

	// now apply the constants

	// the way we apply constants is correctionMethod1 (first the rotations, second the shifts)
	// to revert the alignment transformation properly, the constants have to be applied in
	// a reverted way, i.e. first the shifts, second the rotations

	// not that the sign is different for all the constants wrt to what is done in EUTelApplyAlignmentProcessor -
	// the transformation is reverted

	// first the shifts
	x += c->getXOffset();
	y += c->getYOffset();
	z += c->getZOffset();

	double	x_temp = x;
	double	y_temp = y;
	double	z_temp = z;

	double	alpha = c -> getAlpha();
	double	beta  = c -> getBeta();
	double	gamma = c -> getGamma();

	// second the rotation
	// for the inverse matrix derivation see paper log book 19/01/2011
	// libov@mail.desy.de
// no correct any more due to changes in convention of the angle signs!
// further more a more clear way would be to multiply an input vector by an inverse rotation matrix  
// Igor Rubinsky 09-10-2011


	x = x_temp * (1 + alpha * alpha ) + ( (-1) * gamma - alpha * beta) * y_temp + ( (-1) * beta + alpha * gamma) * z_temp;
	y = x_temp * (gamma - alpha * beta ) + (1 + beta * beta) * y_temp + ((-1) * alpha - beta * gamma) * z_temp;
	z = x_temp * (beta + alpha * gamma ) + (alpha - gamma * beta) * y_temp + ( 1 + gamma * gamma) * z_temp;

	double det = 1 + alpha * alpha + beta * beta + gamma * gamma;

	x = x / det;
	y = y / det;
	z = z / det;
}



void EUTelApplyAlignmentProcessor::_EulerRotation(int sensorID, double* _telPos, double* _gRotation) {
   
    try{
        double doesNothing = _telPos[2];
        doesNothing++;
    }
    catch(...)
    {
        throw InvalidParameterException("_telPos[] array can not be accessed \n");
    }

    TVector3 _RotatedSensorHit( _telPos[0], _telPos[1], _telPos[2] );

    if( TMath::Abs(_gRotation[2]) > 1e-6 )    _RotatedSensorHit.RotateX( _gRotation[2] ); // in ZY
    if( TMath::Abs(_gRotation[1]) > 1e-6 )    _RotatedSensorHit.RotateY( _gRotation[1] ); // in ZX 
    if( TMath::Abs(_gRotation[0]) > 1e-6 )    _RotatedSensorHit.RotateZ( _gRotation[0] ); // in XY

    _telPos[0] = _RotatedSensorHit.X();
    _telPos[1] = _RotatedSensorHit.Y();
    _telPos[2] = _RotatedSensorHit.Z();
 
}


void EUTelApplyAlignmentProcessor::_EulerRotationInverse(int sensorID, double* _telPos, double* _gRotation) {
   
    try{
        double doesNothing = _telPos[2];
        doesNothing++;
    }
    catch(...)
    {
        throw InvalidParameterException("_telPos[] array can not be accessed \n");
    }

    TVector3 _RotatedSensorHit( _telPos[0], _telPos[1], _telPos[2] );

    if( TMath::Abs(_gRotation[0]) > 1e-6 )    _RotatedSensorHit.RotateZ( -1.*_gRotation[0] ); // in XY
    if( TMath::Abs(_gRotation[1]) > 1e-6 )    _RotatedSensorHit.RotateY( -1.*_gRotation[1] ); // in ZX 
    if( TMath::Abs(_gRotation[2]) > 1e-6 )    _RotatedSensorHit.RotateX( -1.*_gRotation[2] ); // in ZY
 

    _telPos[0] = _RotatedSensorHit.X();
    _telPos[1] = _RotatedSensorHit.Y();
    _telPos[2] = _RotatedSensorHit.Z();
 
}

void EUTelApplyAlignmentProcessor::AlignReferenceHit(EUTelEventImpl * evt, EUTelAlignmentConstant * alignment )
{
    int iPlane = alignment->getSensorID();
    streamlog_out( DEBUG )<< "AlignReferenceHit for sensor ID:" <<  alignment->getSensorID() << endl;
    double            alpha   = alignment->getAlpha();
    double            beta    = alignment->getBeta();
    double            gamma   = alignment->getGamma();
    double            offsetX = alignment->getXOffset();
    double            offsetY = alignment->getYOffset();
    double            offsetZ = alignment->getZOffset();
                
                streamlog_out ( DEBUG   )  << " alignment->getAlpha() = " << alpha  << endl;
                streamlog_out ( DEBUG   )  << " alignment->getBeta()  = " <<  beta  << endl;
                streamlog_out ( DEBUG   )  << " alignment->getGamma() = " << gamma << endl;
                streamlog_out ( DEBUG   )  << " alignment->getXOffest() = " << offsetX  << endl;
                streamlog_out ( DEBUG   )  << " alignment->getYOffest() = " << offsetY  << endl;
                streamlog_out ( DEBUG   )  << " alignment->getZOffest() = " << offsetZ  << endl;

    if( evt == 0 )
    {
      streamlog_out(ERROR) << "EMPTY event!" << endl;
      return;
    } 
    
    if( _referenceHitVec == 0 )
    {
      streamlog_out(MESSAGE) << "_referenceHitVec is empty" << endl;
    }
   
    streamlog_out( DEBUG ) << "reference Hit collection name : " << _referenceHitCollectionName.c_str() << " elements " << _referenceHitVec->getNumberOfElements() <<endl;
 
      for(size_t ii = 0 ; ii <  (unsigned int)_referenceHitVec->getNumberOfElements(); ii++)
      {
        EUTelReferenceHit * refhit        = static_cast< EUTelReferenceHit*> ( _referenceHitVec->getElementAt(ii) ) ;
        EUTelReferenceHit * output_refhit = 0;
        try{ 
          output_refhit = static_cast< EUTelReferenceHit*> ( _outputReferenceHitVec->getElementAt(ii) ) ;
          streamlog_out( DEBUG ) << "existing refhit["<<ii<<"] found at  " << output_refhit << endl;
          
        }
        catch(...)
        {
          streamlog_out( DEBUG ) << " creating new EUTelReferenceHit element " << ii << endl;
          output_refhit = new EUTelReferenceHit();
          streamlog_out( DEBUG ) << "new refhit for ["<<ii<<"] created at  " << output_refhit << endl;
          _outputReferenceHitVec->push_back(output_refhit);
        } 

        if( iPlane != refhit->getSensorID() )
        {
          streamlog_out( DEBUG ) << "Looping through a varity of sensor IDs for iPlane :"<< iPlane <<" and refhit->getSensorID()="<<refhit->getSensorID() << endl;
          continue;
        }
        else
        {
          output_refhit->setSensorID(iPlane);
          streamlog_out( DEBUG ) << "Sensor ID and Alignment plane ID match!" << endl;
        }

       streamlog_out(DEBUG  ) << "reference Hit collection name : " << _referenceHitCollectionName.c_str() << " elements " << _referenceHitVec->getNumberOfElements() << " ";
       streamlog_out(DEBUG  ) << " at : " << _referenceHitVec << " ";
       streamlog_out(DEBUG  ) << "PRE sensorID: " <<  refhit->getSensorID(   )     
                               << " x    :" <<        refhit->getXOffset(    )    
                               << " y    :" <<        refhit->getYOffset(    )    
                               << " z    :" <<        refhit->getZOffset(    )    
                               << " alfa :" <<        refhit->getAlpha()          
                               << " beta :" <<        refhit->getBeta()           
                               << " gamma:" <<        refhit->getGamma()        << endl ;

        if( GetApplyAlignmentDirection() == 0) 
        {
           output_refhit->setXOffset( refhit->getXOffset() - offsetX );
           output_refhit->setYOffset( refhit->getYOffset() - offsetY );
           output_refhit->setZOffset( refhit->getZOffset() - offsetZ );

           TVector3 _RotatedVector( refhit->getAlpha(), refhit->getBeta(), refhit->getGamma() );
           _RotatedVector.RotateX(  -alpha        ); // in ZY
           _RotatedVector.RotateY(  -beta         ); // in ZY
           _RotatedVector.RotateZ(  -gamma        ); // in XY 
  
           output_refhit->setAlpha( _RotatedVector[0] );
           output_refhit->setBeta( _RotatedVector[1] );
           output_refhit->setGamma( _RotatedVector[2] );
        }else{
           output_refhit->setXOffset( refhit->getXOffset() + offsetX );
           output_refhit->setYOffset( refhit->getYOffset() + offsetY );
           output_refhit->setZOffset( refhit->getZOffset() + offsetZ );

           TVector3 _RotatedVector( refhit->getAlpha(), refhit->getBeta(), refhit->getGamma() );
           _RotatedVector.RotateZ(  +gamma        ); // in XY 
           _RotatedVector.RotateY(  +beta         ); // in ZY
           _RotatedVector.RotateX(  +alpha        ); // in ZY
 
           output_refhit->setAlpha( _RotatedVector[0] );
           output_refhit->setBeta( _RotatedVector[1] );
           output_refhit->setGamma( _RotatedVector[2] );
        }
//        referenceHitCollection->push_back( refhit );
 
      streamlog_out(MESSAGE) << "reference Hit collection name : " << _outputReferenceHitCollectionName.c_str() << " elements " << _outputReferenceHitVec->getNumberOfElements() << " ";
      streamlog_out(MESSAGE) << " at : " << _outputReferenceHitVec << " ";
      streamlog_out(MESSAGE) << "AFT sensorID: " <<  output_refhit->getSensorID(   )     
                              << " x    :" <<        output_refhit->getXOffset(    )    
                              << " y    :" <<        output_refhit->getYOffset(    )    
                              << " z    :" <<        output_refhit->getZOffset(    )    
                              << " alfa :" <<        output_refhit->getAlpha()          
                              << " beta :" <<        output_refhit->getBeta()           
                              << " gamma:" <<        output_refhit->getGamma()        << endl ;
   
      //  _referenceHitVecAligned->push_back( refhit );
     
    }

}

LCCollectionVec* EUTelApplyAlignmentProcessor::CreateDummyReferenceHitCollection()
{

  LCCollectionVec * referenceHitCollection = new LCCollectionVec( LCIO::LCGENERICOBJECT );

  for(size_t ii = 0 ; ii <  _orderedSensorIDVec.size(); ii++)
  {
    EUTelReferenceHit * refhit = new EUTelReferenceHit();
  
    refhit->setSensorID( _orderedSensorIDVec[ii] );
    refhit->setXOffset( 0. );
    refhit->setYOffset( 0. );
    refhit->setZOffset( 0. );

    refhit->setAlpha( 0. ) ;
    refhit->setBeta( 0. );
    refhit->setGamma( 0. );

    referenceHitCollection->push_back( refhit );
  }

  return referenceHitCollection;
}

int EUTelApplyAlignmentProcessor::guessSensorID(const double * hit ) 
{

  int sensorID = -1;
  double minDistance =  numeric_limits< double >::max() ;

//  message<MESSAGE> ( log() <<  "referencehit collection: " << _referenceHitCollectionName << " at "<< _referenceHitVec);
//  LCCollectionVec * referenceHitVec     = dynamic_cast < LCCollectionVec * > (evt->getCollection( _referenceHitCollectionName));

  if( _referenceHitVec == 0 || _applyToReferenceHitCollection == false)
  {
    // use z information of planes instead of reference vector
    for ( int iPlane = 0 ; iPlane < _siPlanesLayerLayout->getNLayers(); ++iPlane ) {
      double distance = std::abs( hit[2] - _siPlaneZPosition[ iPlane ] );
      if ( distance < minDistance ) {
	minDistance = distance;
	sensorID = _siPlanesLayerLayout->getID( iPlane );
      }
    }
    if ( minDistance > 30  ) {
      // advice the user that the guessing wasn't successful 
      streamlog_out( WARNING3 ) << "A hit was found " << minDistance << " mm far from the nearest plane\n"
	"Please check the consistency of the data with the GEAR file: hitPosition[2]=" << hit[2] <<       endl;
    }
    
    return sensorID;
  }

//  message<MESSAGE> ( log() <<  "number of elements : " << _referenceHitVec->getNumberOfElements() << endl );

      for(size_t ii = 0 ; ii <  (unsigned int)_referenceHitVec->getNumberOfElements(); ii++)
      {
        EUTelReferenceHit* refhit = static_cast< EUTelReferenceHit*> ( _referenceHitVec->getElementAt(ii) ) ;
        if(refhit == 0 ) continue;
       
        TVector3 hit3d( hit[0], hit[1], hit[2] );
        TVector3 hitInPlane( refhit->getXOffset(), refhit->getYOffset(), refhit->getZOffset());
        TVector3 norm2Plane( refhit->getAlpha(), refhit->getBeta(), refhit->getGamma() );
 
        double distance = abs( norm2Plane.Dot(hit3d-hitInPlane) );
        if ( distance < minDistance ) 
        {
           minDistance = distance;
           sensorID = refhit->getSensorID();
        }    

      }

/*      for(size_t ii = 0 ; ii <  _referenceHitVec->getNumberOfElements(); ii++)
      {
        EUTelReferenceHit* refhit = static_cast< EUTelReferenceHit*> ( _referenceHitVec->getElementAt(ii) ) ;
        if(refhit == 0 ) continue;
        if( sensorID != refhit->getSensorID() )  continue;
        message<MESSAGE> ( log() <<" _referenceHitVec "<<_referenceHitVec << " " << _referenceHitCollectionName.c_str()<< " " << refhit << 
                                                 " at " << refhit->getXOffset() << " " <<refhit->getYOffset() << " " << refhit->getZOffset() <<
                                                 " normal: " << refhit->getAlpha() << " " << refhit->getBeta() << " " << refhit->getGamma() << endl) ; 
        message<MESSAGE> ( log() << "iPlane " << refhit->getSensorID() << " hitPos:  [" << hit[0] << " " << hit[1] << " " <<  hit[2] << "]  distance: " <<  minDistance  << endl );
      }*/
  
  return sensorID;
}


void EUTelApplyAlignmentProcessor::DumpReferenceHitDB(std::string name="referencehit" )
{
// create a reference hit collection file (DB)

  LCWriter * lcWriter = LCFactory::getInstance()->createLCWriter();
  try {
    lcWriter->open( _referenceHitLCIOFile, LCIO::WRITE_APPEND );
  } catch ( IOException& e ) {
    streamlog_out ( ERROR4 ) << e.what() << endl;
    exit(-1);
  }

  LCEventImpl * event = new LCEventImpl;

  streamlog_out ( MESSAGE ) << "Writing to " << _referenceHitLCIOFile << std::endl;

  LCCollectionVec * referenceHitCollection = new LCCollectionVec( LCIO::LCGENERICOBJECT );
  for(size_t ii = 0 ; ii <  _orderedSensorIDVec.size(); ii++)
  {
    EUTelReferenceHit * refhit = new EUTelReferenceHit();
    refhit->setSensorID( _orderedSensorIDVec[ii] );
    refhit->setXOffset( 0. );
    refhit->setYOffset( 0. );
    refhit->setZOffset( 0. );
   
    refhit->setAlpha( 0. );
    refhit->setBeta( 0. );
    refhit->setGamma( 0. );

    referenceHitCollection->push_back( refhit );
  }
  event->addCollection( referenceHitCollection, "referenceHit" );
  lcWriter->writeEvent( event );
  delete event;
}
#endif
