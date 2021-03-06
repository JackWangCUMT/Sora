#pragma once

#include "ieee80211facade.hpp"
#include "depuncturer.hpp"

typedef struct _tagBB11aDemodContext :
	  LOCAL_CONTEXT(TDropAny)
  	, LOCAL_CONTEXT(TBB11aFrameSink)									  	  	
  	, LOCAL_CONTEXT(T11aViterbi)									  
	, LOCAL_CONTEXT(T11aPLCPParser)	  		  		  		  		  	
	, LOCAL_CONTEXT(T11aDemap)	  		  		  		  	
	, LOCAL_CONTEXT(TPilotTrack)	  		  		  	
	, LOCAL_CONTEXT(TChannelEqualization)	  		  	
	, LOCAL_CONTEXT(T11aLTSymbol)	  	
	, LOCAL_CONTEXT(TChannelEst)	
	, LOCAL_CONTEXT(TCCA11a)  
	, LOCAL_CONTEXT(TPhaseCompensate)  	
	, LOCAL_CONTEXT(TFFT64)  		
	, LOCAL_CONTEXT(TFreqCompensation)  			
	, LOCAL_CONTEXT(TDCRemove)  
	, LOCAL_CONTEXT(TMemSamples)
    , LOCAL_CONTEXT(TBB11aRxRateSel)
    , LOCAL_CONTEXT(T11aLTS)        
{
	void Reset () {
		// Reset all CFacade data in the context
		// CF_Error
		CF_Error::error_code() = E_ERROR_SUCCESS;

		// CF_11CCA
		CF_11CCA::Reset();

		// CF_Channel_11a
		CF_Channel_11a::Reset ();

		// CF_11aRxVector
		CF_11aRxVector::Reset ();

		// CF_FreqOffsetComp
		CF_FreqCompensate::Reset ();

		// Phase 
		CF_PhaseCompensate::Reset ();

		// CF_PilotTrack
		CF_PilotTrack::Reset ();

		// CF_11aSymSel
		CF_11aSymState::Reset(); 

		// CF_CFOffset
		CF_CFOffset::Reset ();
		
		// CF_11RxPLCPSwitch
		CF_11RxPLCPSwitch::Reset();

        // CF_11aRxVector
        CF_11aRxVector::Reset();
	}
	
	void Init ( COMPLEX16* input, uint in_size, UCHAR* output, uint out_size ) 
	{
		// CF_11CCA
		CF_11CCA::cca_pwr_threshold() = 1000*1000; // set energy threshold

		// CF_MemSamples
		CF_MemSamples::Init ( input, in_size );

		// CF_VecDC
		CF_VecDC::Reset ();
/*		for (int i=0; i<4; i++) {
			CF_VecDC::direct_current()[i].re = 1458;
			CF_VecDC::direct_current()[i].im = -573;
		}
 */		

		// CF_RxFrameBuffer
		CF_RxFrameBuffer::Init (output, out_size);

		Reset ();
		
	}

	
	void CCA_OnPowerDetected () {
//		printf ( "CCA ::=> Power detected at %d\n", CF_MemSamples::mem_sample_index() );
	}

	void CCA_OnCCADone () {
		printf ( "CCA ::=> CCA done at %d\n", CF_MemSamples::mem_sample_index() );
	}

}BB11aDemodContext;
	  
	  
BB11aDemodContext BB11aDemodCtx;

struct sym_selector {
	FINL
	int operator()(BB11aDemodContext& ctx) {
		if ( ctx.CF_11aSymState::symbol_type()  == CF_11aSymState::SYMBOL_TRAINING ) { 
			return PORT_0; 
		} else {
		 	return PORT_1;
		}
	}
};
	  
typedef TGDemux<2, COMPLEX16, 4, sym_selector> T11aSymSelEx;


//
// Define the selector object for the plcp switch
//
struct plcp_selector {
	FINL
	int operator()(BB11aDemodContext& ctx) {
		if ( ctx.CF_11RxPLCPSwitch::plcp_state()  == CF_11RxPLCPSwitch::plcp_header ) { 
			return PORT_0;
		} else {
		 	return PORT_1;
        }
	}
};
	  
/*************************************************************************
Processing graph

src-> downsample -> rxswt -> dc_removedc -> BB11aCCA -> estimate_dc 
                     |-> symsel -> LTSSymbol -> ch_est -> fine_cfo -> drop
                               |-> dataSym -> FFT -> Equalization -> phase_comp ..-> 
  ..-> pilot -> plcpswt -> plcp_demap -> plcp_deinter -> plcp_viterbi -> plcp_parser
                      | -> 6M_demap -> 6M_deinter -> thread_sep -> viterbi -> desc -> frame_sink

*************************************************************************/

// Note: use "static inline" instead of "inline" to prevent silent function confliction during linking.
// Otherwise, it will introduce wrong linking result because these inline function share the same name,
// and indeed not inlined.
// ref: http://stackoverflow.com/questions/2217628/multiple-definition-of-inline-functions-when-linking-static-libs
static inline
void CreateDemodGraph11a (ISource*& src1, ISource*& src2)
{
    CREATE_BRICK_SINK  (drop, TDropAny,    BB11aDemodCtx );
	CREATE_BRICK_SINK  (err , TFrameError, BB11aDemodCtx );

    CREATE_BRICK_SINK   (fsink,   TBB11aFrameSink, BB11aDemodCtx );	
	CREATE_BRICK_FILTER (desc,    T11aDesc,	    BB11aDemodCtx, fsink );	
	typedef T11aViterbi<5000*8,   48, 24> T11aViterbi6M;
	typedef T11aViterbi<5000*8,   48*2, 24*8> T11aViterbi24M;	
	typedef T11aViterbi<5000*8, 48*6*4/3, 192> T11aViterbi48M;
	CREATE_BRICK_FILTER (viterbi,  T11aViterbi6M::Filter,  BB11aDemodCtx, desc );				
    CREATE_BRICK_FILTER (vit1, TThreadSeparator<128>::Filter, BB11aDemodCtx, viterbi);
    CREATE_BRICK_FILTER (vit0, TNoInline, BB11aDemodCtx, vit1);
	
//    CREATE_BRICK_FILTER (vit0, TNoInline, BB11aDemodCtx, viterbi);
//    CREATE_BRICK_SINK  (vit0, TDropAny,    BB11aDemodCtx );


	// 6M
	CREATE_BRICK_FILTER (di6, 	T11aDeinterleaveBPSK, BB11aDemodCtx, vit0 );		
    CREATE_BRICK_FILTER (dm6, 	T11aDemapBPSK::Filter,        BB11aDemodCtx, di6 );
#if 0

	// 12M
	CREATE_BRICK_FILTER (di12, 	T11aDeinterleaveQPSK, BB11aDemodCtx, vit0 );		
	CREATE_BRICK_FILTER (dm12, 	T11aDemapQPSK,      BB11aDemodCtx, di12 );
	
#endif	
	// 24M
	CREATE_BRICK_FILTER (di24, 	T11aDeinterleaveQAM16, BB11aDemodCtx, vit1 );		
	CREATE_BRICK_FILTER (dm24, 	T11aDemapQAM16::Filter,      BB11aDemodCtx, di24 );

	// 48M
	CREATE_BRICK_FILTER (di48, 	T11aDeinterleaveQAM64, BB11aDemodCtx, vit0 );		
	CREATE_BRICK_FILTER (dm48, 	T11aDemapQAM64::Filter,        BB11aDemodCtx, di48 );

	// PLCP 
    CREATE_BRICK_SINK   (plcp,      T11aPLCPParser, BB11aDemodCtx );
	CREATE_BRICK_FILTER (sviterbik, T11aViterbiSig, BB11aDemodCtx, plcp );
	CREATE_BRICK_FILTER (dibpsk, 	T11aDeinterleaveBPSK, BB11aDemodCtx, sviterbik );		
	CREATE_BRICK_FILTER (dmplcp, 	T11aDemapBPSK::Filter, BB11aDemodCtx, dibpsk );

	CREATE_BRICK_DEMUX5 ( sigsel, TBB11aRxRateSel,	BB11aDemodCtx,
						  dmplcp, dm6, err, dm24, dm48 );
	
//	CREATE_BRICK_FILTER ( sigsel0,  TNoInline,      BB11aDemodCtx, sigsel );		
	
	CREATE_BRICK_FILTER (pilot,  TPilotTrack, 			BB11aDemodCtx, sigsel );		
	CREATE_BRICK_FILTER (pcomp,  TPhaseCompensate, 		BB11aDemodCtx, pilot );	
	CREATE_BRICK_FILTER (chequ,  TChannelEqualization, 	BB11aDemodCtx, pcomp );		
	CREATE_BRICK_FILTER (fft,    TFFT64, 				BB11aDemodCtx, chequ );		
	CREATE_BRICK_FILTER (fcomp,  TFreqCompensation, 	BB11aDemodCtx, fft );				
	CREATE_BRICK_FILTER (dsym,   T11aDataSymbol, 		BB11aDemodCtx, fcomp );		
	CREATE_BRICK_FILTER (dsym0,  TNoInline,      		BB11aDemodCtx, dsym );		

	// LTS path
//	CREATE_BRICK_FILTER (fcfo,  TFineCFOEst, BB11aDemodCtx, drop );		
//	CREATE_BRICK_FILTER (chest, TChannelEst, BB11aDemodCtx, fcfo );	
//	CREATE_BRICK_FILTER (lsym,  T11aLTSymbol, BB11aDemodCtx, chest );	
	CREATE_BRICK_SINK (lsym,	T11aLTS, BB11aDemodCtx );	

	CREATE_BRICK_DEMUX2 (symsel, T11aSymSelEx::Filter, BB11aDemodCtx, 
			lsym, dsym0 );	

	CREATE_BRICK_FILTER (dcest, TDCEstimator, BB11aDemodCtx, drop );
	CREATE_BRICK_FILTER (cca,   TCCA11a,      BB11aDemodCtx, dcest );
	CREATE_BRICK_FILTER (dc,    TDCRemoveEx<4>::Filter, BB11aDemodCtx, cca );

	CREATE_BRICK_DEMUX2 (rxswt, TBB11bRxSwitch, BB11aDemodCtx,
			dc, symsel);

	CREATE_BRICK_FILTER (ds2, TDownSample2, BB11aDemodCtx, rxswt );
    CREATE_BRICK_SOURCE (fsrc, TMemSamples, BB11aDemodCtx, ds2 );

    src1 = fsrc;
	src2 = vit1;
//	src2 = fsrc;

}



