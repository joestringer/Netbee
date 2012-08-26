/*****************************************************************************/
/*                                                                           */
/* Copyright notice: please read file license.txt in the NetBee root folder. */
/*                                                                           */
/*****************************************************************************/



#include "netpflfrontend.h"
#include "statements.h"
#include "ircodegen.h"
#include "compile.h"
#include "dump.h"
#include "encapfsa.h"
#include "sft/sft_writer.hpp"
#include "compilerconsts.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "cfgwriter.h"
#include "../nbee/globals/debug.h"
#include "protographwriter.h"
#include "filtersubgraph.h"
#include "protographshaper.h"
#include "cfg_edge_splitter.h"
#include "cfgdom.h"
#include "cfg_ssa.h"
#include "deadcode_elimination_2.h"
#include "constant_propagation.h"
#include "copy_propagation.h"
#include "constant_folding.h"
#include "controlflow_simplification.h"
#include "redistribution.h"
#include "reassociation.h"
#include "irnoderegrenamer.h"
#include "../nbnetvm/jit/copy_folding.h"
#include "register_mapping.h"
#include "reassociation_fixer.h"
#include "optimizer_statistics.h"
#include "nbpflcompiler.h"
#include <map>



#ifdef ENABLE_PFLFRONTEND_PROFILING
#include <nbee_profiler.h>
#endif


using namespace std;


void NetPFLFrontEnd::SetupCompilerConsts(CompilerConsts &compilerConsts, uint16 linkLayer)
{
	compilerConsts.AddConst("$linklayer", linkLayer);
}

//TODO OM: maybe it would be better to keep a smaller constructor and to provide some "Set" methods for
// setting all those filenames. Moreover, the unsigned char flags (like dumpHIRCode) are useless if we check
// the presence of the corresponding filename

NetPFLFrontEnd::NetPFLFrontEnd(_nbNetPDLDatabase &protoDB, nbNetPDLLinkLayer_t LinkLayer,
										 const unsigned int DebugLevel,
										 const char *dumpHIRCodeFilename,
			 							 const char *dumpMIRNoOptGraphFilename,
										 const char *dumpProtoGraphFilename)
										 :m_ProtoDB(&protoDB), m_GlobalInfo(protoDB), m_GlobalSymbols(m_GlobalInfo),
										  m_CompUnit(0), m_MIRCodeGen(0), m_NetVMIRGen(0),  m_StartProtoGenerated(0),
									      NetPDLParser(protoDB, m_GlobalSymbols, m_ErrorRecorder),
									      HIR_FilterCode(0), NetVMIR_FilterCode(0), PFL_Tree(0),
										 NetIL_FilterCode("")
#ifdef OPTIMIZE_SIZED_LOOPS
										 , m_ReferredFieldsInFilter(0), m_ReferredFieldsInCode(0), m_ContinueParsingCode(false)
#endif
{
	m_GlobalInfo.Debugging.DebugLevel= DebugLevel;

	if (dumpHIRCodeFilename!=NULL)
		m_GlobalInfo.Debugging.DumpHIRCodeFilename=(char *)dumpHIRCodeFilename;
	if (dumpMIRNoOptGraphFilename!=NULL)
		m_GlobalInfo.Debugging.DumpMIRNoOptGraphFilename=(char *)dumpMIRNoOptGraphFilename;
	if (dumpProtoGraphFilename!=NULL)
		m_GlobalInfo.Debugging.DumpProtoGraphFilename=(char *)dumpProtoGraphFilename;

	if (m_GlobalInfo.Debugging.DebugLevel > 0)
		nbPrintDebugLine("Initializing NetPFL Front End...", DBG_TYPE_INFO, __FILE__, __FUNCTION__, __LINE__, 1);

	if (!m_GlobalInfo.IsInitialized())
		throw ErrorInfo(ERR_FATAL_ERROR, "startproto protocol not found!!!");

#ifdef ENABLE_PFLFRONTEND_PROFILING
	uint64_t TicksBefore, TicksAfter, TicksTotal, MeasureCost;

	TicksTotal= 0;
	MeasureCost= nbProfilerGetMeasureCost();
#endif

	CompilerConsts &compilerConsts = m_GlobalInfo.GetCompilerConsts();
	SetupCompilerConsts(compilerConsts, LinkLayer);

	ofstream EncapCode;

	if (m_GlobalInfo.Debugging.DumpHIRCodeFilename)
		EncapCode.open(m_GlobalInfo.Debugging.DumpHIRCodeFilename);

	CodeWriter cw(EncapCode);

#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksBefore= nbProfilerGetTime();
#endif

	HIRCodeGen ircodegen(m_GlobalSymbols, &(m_GlobalSymbols.GetProtoCode(m_GlobalInfo.GetStartProtoSym())));
	NetPDLParser.ParseStartProto(ircodegen, PARSER_VISIT_ENCAP);
	SymbolProto **protoList = m_GlobalInfo.GetProtoList();

#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksAfter= nbProfilerGetTime();
	TicksTotal= TicksTotal + (TicksAfter - TicksBefore - MeasureCost);

	protoList[0]->ParsingTime= TicksAfter - TicksBefore - MeasureCost;
#endif
	if (m_GlobalInfo.Debugging.DumpHIRCodeFilename)
	{//this generates di HIL code fot the startproto
		cw.DumpCode(&m_GlobalSymbols.GetProtoCode(protoList[0]));
	}
	
	//create the graph
	for (uint32 i = 1; i < m_GlobalInfo.GetNumProto(); i++)
	{
#ifdef ENABLE_PFLFRONTEND_PROFILING
		TicksBefore= nbProfilerGetTime();
#endif

		HIRCodeGen protocodegen(m_GlobalSymbols, &(m_GlobalSymbols.GetProtoCode(protoList[i])));
		NetPDLParser.ParseProtocol(*protoList[i], protocodegen, PARSER_VISIT_ENCAP);

#ifdef ENABLE_PFLFRONTEND_PROFILING
		TicksAfter= nbProfilerGetTime();
		TicksTotal= TicksTotal + (TicksAfter - TicksBefore - MeasureCost);

		protoList[i]->ParsingTime= TicksAfter - TicksBefore - MeasureCost;
#endif

		//if (!protoList[i]->IsSupported)
		//	if (m_GlobalInfo.Debugging.FlowInformation > 0)
		//		cerr << ";Protocol " << protoList[i]->Name << " is not supported" << endl;
		//if (!protoList[i]->IsDefined)
		//	cerr << ";Protocol " << protoList[i]->Name << " is not defined" << endl;

		if (m_GlobalInfo.Debugging.DumpHIRCodeFilename)
		{//this generates the HIR code for the i-th protocol
			cw.DumpCode(&m_GlobalSymbols.GetProtoCode(protoList[i]));
		}
	}

#ifdef STATISTICS
	ofstream fieldStats;
		fieldStats.open("fieldstats.txt");
		m_GlobalSymbols.DumpProtoFieldStats(fieldStats);
	printf("\nTotal protocols number: %u", m_GlobalInfo.GetNumProto());

	uint32 notSupportedProto=0;
	FILE *f=fopen("statistics.csv", "w");
	if (f!=NULL)
	{
		fprintf(f, "Protocol name;Supported;Encapsulable;Parsing time (ticks);" \
			"ParsedFields;Inner fields;Field unsupported;Ref field unknown;" \
			"Expression unsupported;Var declared;Var unsupported;Var occurrences;Var tot occurrences;" \
			"Encap declared;Encap tentative;Encap successful;" \
			"Lookup declared;Lookup occurrences" \
			"\n");
		for (uint32 i = 0; i < m_GlobalInfo.GetNumProto(); i++)
		{
			fprintf(f, "%s", protoList[i]->Name.c_str());

			if (protoList[i]->IsDefined==false || protoList[i]->IsSupported==false)
				fprintf(f, ";no");
			else
				fprintf(f, ";yes");

			if (protoList[i]->VerifySectionSupported==false || protoList[i]->BeforeSectionSupported==false)
				fprintf(f, ";no");
			else
				fprintf(f, ";yes");

			fprintf(f, ";-");
			fprintf(f, ";%u", protoList[i]->DeclaredFields);
			fprintf(f, ";%u", protoList[i]->InnerFields);
			fprintf(f, ";%u", protoList[i]->UnsupportedFields);
			fprintf(f, ";%u", protoList[i]->UnknownRefFields);
			fprintf(f, ";%u", protoList[i]->UnsupportedExpr);
			fprintf(f, ";%u", protoList[i]->VarDeclared);
			fprintf(f, ";%u", protoList[i]->VarUnsupported);
			fprintf(f, ";%u", protoList[i]->VarOccurrences);
			fprintf(f, ";%u", protoList[i]->VarTotOccurrences);
			fprintf(f, ";%u", protoList[i]->EncapDecl);
			fprintf(f, ";%u", protoList[i]->EncapTent);
			fprintf(f, ";%u", protoList[i]->EncapSucc);
			fprintf(f, ";%u", protoList[i]->LookupDecl);
			fprintf(f, ";%u", protoList[i]->LookupOccurrences);
			fprintf(f, "\n");
			if (protoList[i]->IsDefined==false || protoList[i]->IsSupported==false)
				notSupportedProto++;
		}
	}

	printf("\n\tTotal unsupported protocols number: %u\n", notSupportedProto);
#endif

#ifdef ENABLE_PFLFRONTEND_PROFILING
	printf("\n\n\tProtocol name  Parsing time (ticks)\n");
	for (uint32 i = 0; i < m_GlobalInfo.GetNumProto(); i++)
	{
		printf("\t%s  %ld\n", protoList[i]->Name.c_str(), protoList[i]->ParsingTime);
	}
	printf("\n\tTotal protocol parsing time %ld (avg per protocol %ld)\n\n", TicksTotal, TicksTotal/m_GlobalInfo.GetNumProto());
#endif

        if (m_GlobalInfo.Debugging.DumpHIRCodeFilename)
          EncapCode.close();

#ifndef ENABLE_PFLFRONTEND_PROFILING
	//ProtoGraphLayers protoLayers(m_GlobalInfo.GetProtoGraph(), *m_GlobalInfo.GetStartProtoSym()->GraphNode);
	//protoLayers.FindProtoLevels();

	if (m_GlobalInfo.Debugging.DumpProtoGraphFilename)
	{
		ofstream protograph(m_GlobalInfo.Debugging.DumpProtoGraphFilename);
		m_GlobalInfo.DumpProtoGraph(protograph);
		protograph.close();
	}
#endif

	if (m_GlobalInfo.Debugging.DebugLevel > 0)
		nbPrintDebugLine("Initialized NetPFL Front End", DBG_TYPE_INFO, __FILE__,
		__FUNCTION__, __LINE__, 1);

}


NetPFLFrontEnd::~NetPFLFrontEnd()
{
	if (m_CompUnit != NULL)
	{
		delete m_CompUnit;
		m_CompUnit = NULL;
	}
}




void NetPFLFrontEnd::DumpPFLTreeDotFormat(PFLExpression *expr, ostream &stream)
{
	stream << "Digraph PFL_Tree{" << endl;
	expr->PrintMeDotFormat(stream);
	stream << endl << "}" << endl;
}

//[icerrato] this method is used by the sample "filtercompiler" with the option "-compileonly"
bool NetPFLFrontEnd::CheckFilter(string filter)
{
	m_ErrorRecorder.Clear();

	ParserInfo parserInfo(m_GlobalSymbols, m_ErrorRecorder);
	compile(&parserInfo, filter.c_str(), 0);

	if (parserInfo.Filter == NULL)
		return false;
	else
		return true;
}

//[icerrato] this method is used by the sample "filtercompiler" with the option "-automaton"
int NetPFLFrontEnd::CreateAutomatonFromFilter(string filter)
{
	m_ErrorRecorder.Clear();

	PFLStatement *filterExpression = ParseFilter(filter);  
	if (filterExpression == NULL)
		return nbFAILURE;

	bool fieldExtraction = false;
	NodeList_t toExtract;

	if (filter.size() > 0) //i.e. if the filter is not empty
	{
		PFLAction *action = filterExpression->GetAction();
		nbASSERT(action, "action cannot be null") 
		if (action->GetType() == PFL_EXTRACT_FIELDS_ACTION)
		{
	   		fieldExtraction = true;
	   		PFLExtractFldsAction *exFldAct = (PFLExtractFldsAction*) action;
			FieldsList_t &fields = exFldAct->GetFields();
			FieldsList_t::iterator f = fields.begin();

			EncapGraph &protograph = m_GlobalInfo.GetProtoGraph();
		
			EncapFSA::SetGraph(&protograph);

			for (; f != fields.end(); f++)
			{
				SymbolField *field = *f;
				toExtract.push_back(protograph.GetNode(field->Protocol));
				toExtract.sort();
				toExtract.unique();
			}		
	   	}

		m_fsa = VisitFilterExpression(filterExpression->GetExpression(),fieldExtraction,toExtract);

		if(m_fsa==NULL)
			return nbFAILURE; //an error occurred during the creation of the automaton
			
		if (fieldExtraction)
	   	{   	
			EncapFSA::Alphabet_t fsaAlphabet;
			EncapGraph &protoGraph = m_GlobalInfo.GetProtoGraph();
        	EncapFSABuilder fsaBuilder(protoGraph, fsaAlphabet);
        	m_fsa = fsaBuilder.ExtractFieldsFSA(toExtract, m_fsa);
		}   

		m_fsa->fixStateProtocols();	
	}
	return nbSUCCESS;		
}


PFLStatement *NetPFLFrontEnd::ParseFilter(string filter)
{
	
	if (filter.size() == 0)
	{
		PFLReturnPktAction *retpkt = new PFLReturnPktAction(1);
		CHECK_MEM_ALLOC(retpkt);
		PFLStatement *stmt = new PFLStatement(NULL, retpkt, NULL);
		CHECK_MEM_ALLOC(stmt);
		return stmt;
	}
	
	ParserInfo parserInfo(m_GlobalSymbols, m_ErrorRecorder); //this struct is defined in compile.h
	compile(&parserInfo, filter.c_str(), 0);
		
	return parserInfo.Filter;//return the filtering expression
}

int NetPFLFrontEnd::CompileFilter(string filter, bool optimizationCycles)
{

	m_ErrorRecorder.Clear();
	if (m_CompUnit != NULL)
	{
		delete m_CompUnit;
		m_CompUnit = NULL;
	}

	PFLStatement *filterStmt = ParseFilter(filter);  //now we have the statement related to the filtering expression 
	if (filterStmt == NULL)
		//return false;
		return nbFAILURE;
	m_CompUnit = new CompilationUnit(filter);
	CHECK_MEM_ALLOC(m_CompUnit);

#ifdef ENABLE_PFLFRONTEND_PROFILING
	int64_t TicksBefore, TicksAfter, TicksTotal, MeasureCost;

	TicksTotal= 0;
	MeasureCost= nbProfilerGetMeasureCost();
#endif

	if (filter.size() > 0) //i.e. if the filter is not empty
	{
#ifdef ENABLE_PFLFRONTEND_PROFILING
		TicksBefore= nbProfilerGetTime();
#endif

		MIRCodeGen mirCodeGen(m_GlobalSymbols, &m_CompUnit->IRCode);
		m_MIRCodeGen = &mirCodeGen;
		m_CompUnit->OutPort = 1;
		if(filterStmt->GetAction()!=NULL)
			VisitAction(filterStmt->GetAction()); 

		try
		{	
			if(GenCode(filterStmt)==nbFAILURE) 
				return nbFAILURE;
		}
		catch(ErrorInfo &e)
		{
			if (e.Type != ERR_FATAL_ERROR)
			{
				m_ErrorRecorder.PDLError(e.Msg);
				return nbFAILURE;
			}
			else
			{
				throw e;
			}
		}
		delete filterStmt;

#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksAfter= nbProfilerGetTime();
	printf("\n\n\tMIR generation used %ld ticks", TicksAfter - TicksBefore - MeasureCost);
	TicksBefore= nbProfilerGetTime();
#endif

#ifdef ENABLE_PFLFRONTEND_DBGFILES
                {
		// FULVIO No really idea what we're dumping here
		// Is it similar to what we dump on mir_code.asm?
		ofstream ir("mir_code.asm");		
		CodeWriter cw(ir);
		cw.DumpCode(&m_CompUnit->IRCode); //dumps the MIR code
		ir.close();
                }
#endif
		//creates the control flow graph - defined in pflcfg.h - and translates it from MIR code to MIRO code
		CFGBuilder builder(m_CompUnit->Cfg); 
		builder.Build(m_CompUnit->IRCode);//m_CompUnit->IRCode contains MIR statements
		
#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksAfter= nbProfilerGetTime();
	printf("\n\n\tMIRO generation used %ld ticks", TicksAfter - TicksBefore - MeasureCost);
	TicksBefore= nbProfilerGetTime();
#endif

#ifdef ENABLE_PFLFRONTEND_DBGFILES
		{
			ofstream irCFG("cfg_miro_no_opt.dot");
			DumpCFG(irCFG, false, false);
			irCFG.close();
		}
#endif

		GenRegexEntries(); 
		GenStringMatchingEntries();


		// generate initialization code (i.e. code for the initialization of coprocessors)
		MIRCodeGen mirInitCodeGen(m_GlobalSymbols, &m_CompUnit->InitIRCode);
		m_MIRCodeGen = &mirInitCodeGen;
		try
		{
			int locals = m_CompUnit->NumLocals;
			GenInitCode(); 
			m_CompUnit->NumLocals = locals;
		}
		catch(ErrorInfo &e)
		{
			if (e.Type != ERR_FATAL_ERROR)
			{
				m_ErrorRecorder.PDLError(e.Msg);
				return nbFAILURE;
			}
			else
			{
				throw e;
			}
		}

#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksAfter= nbProfilerGetTime();
	printf("\n\n\tMIR generation for initialization required %ld ticks", TicksAfter - TicksBefore - MeasureCost);
	TicksBefore= nbProfilerGetTime();
#endif
	
	//creates the control flow graph about the initialization code and translates it from MIR code to MIRO code
	CFGBuilder initBuilder(m_CompUnit->InitCfg);
	initBuilder.Build(m_CompUnit->InitIRCode);

#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksAfter= nbProfilerGetTime();
	printf("\n\n\tMIRO generation for initialization required %ld ticks", TicksAfter - TicksBefore - MeasureCost);
	TicksBefore= nbProfilerGetTime();
#endif


#ifdef ENABLE_PFLFRONTEND_DBGFILES
	{
		ofstream ir("cfg_before_opt.dot");
		DumpCFG(ir, false, false);
		ir.close();
	}
#endif

		//*******************************************************************
		//*******************************************************************
		//		HERE IT BEGINS												*
		//*******************************************************************

#ifdef _DEBUG_OPTIMIZER
		std::cout << "Running setCFG" << std::endl;
#endif
		MIRONode::setCFG(&m_CompUnit->Cfg);
//while(false){
		if (optimizationCycles)
		{
#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running NodeRenamer" << std::endl;
#endif
			IRNodeRegRenamer inrr(m_CompUnit->Cfg);
			inrr.run();

#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running edgeSPlitter" << std::endl;
#endif
			CFGEdgeSplitter<MIRONode> cfges(m_CompUnit->Cfg);
			cfges.run();

#ifdef ENABLE_PFLFRONTEND_DBGFILES
			{
				ofstream ir("cfg_edge_splitting.dot");
				DumpCFG(ir, false, false);
				ir.close();
			}
#endif

#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running ComputeDominance" << std::endl;
#endif
			jit::ComputeDominance<PFLCFG> cd(m_CompUnit->Cfg);
			cd.run();

#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running SSA" << std::endl;
#endif
			jit::SSA<PFLCFG> ssa(m_CompUnit->Cfg);
			ssa.run();

#ifdef ENABLE_PFLFRONTEND_DBGFILES
			{
				ofstream ir("cfg_after_SSA.dot");
				DumpCFG(ir, false, false);
				ir.close();
			}
#endif
			bool changed = true;
			bool reass_changed = true;
#ifdef ENABLE_PFLFRONTEND_DBGFILES
			int counter = 0;
#endif

			while(reass_changed) //FIXME: [icerrato] queste ottimizzazioni (quelle nel ciclo while) danno problemi con matches e contains
			{
				reass_changed = false;
				changed = true;

				while (changed)
				{
					changed = false;

#ifdef _DEBUG_OPTIMIZER
					std::cout << "Running Copy propagation" << std::endl;
#endif
					jit::opt::CopyPropagation<PFLCFG> copyp(m_CompUnit->Cfg);
					changed |= copyp.run();
#ifdef _DEBUG_OPTIMIZER
					std::cout << "Running Constant propagation" << std::endl;
#endif
					jit::opt::ConstantPropagation<PFLCFG> cp(m_CompUnit->Cfg);
					changed |= cp.run();

#ifdef _DEBUG_OPTIMIZER
					std::cout << "Running ConstantFolding" << std::endl;
#endif
					jit::opt::ConstantFolding<PFLCFG> cf(m_CompUnit->Cfg);
					changed |= cf.run();

#ifdef _DEBUG_OPTIMIZER
					std::cout << "Running DeadCodeElimination" << std::endl;
#endif
					jit::opt::DeadcodeElimination<PFLCFG> dce_b(m_CompUnit->Cfg);
					changed |= dce_b.run();


#ifdef _DEBUG_OPTIMIZER
					std::cout << "Running Basic block elimination" << std::endl;
#endif
					jit::opt::BasicBlockElimination<PFLCFG> bbs(m_CompUnit->Cfg);
					bbs.start(changed);

#ifdef _DEBUG_OPTIMIZER
					std::cout << "Running Redistribution" << std::endl;
#endif
					jit::opt::Redistribution<PFLCFG> rdst(m_CompUnit->Cfg);
					rdst.start(changed);

				}
#ifdef _DEBUG_OPTIMIZER
				std::cout << "Running reassociation" << std::endl;
#endif
				//{
				//	ostringstream filename;
				//	filename << "cfg_before_reass_" << counter << ".dot";
				//	ofstream ir(filename.str().c_str());
				//	DumpCFG(ir, false, false);
				//	ir.close();
				//}
				{
					jit::Reassociation<PFLCFG> reass(m_CompUnit->Cfg);
					reass_changed = reass.run();
				}
#ifdef ENABLE_PFLFRONTEND_DBGFILES
				{
					ostringstream filename;
					filename << "cfg_after_reass_" << counter++ << ".dot";
					ofstream ir(filename.str().c_str());
					DumpCFG(ir, false, false);
					ir.close();
				}
#endif
			}

			ReassociationFixer rf(m_CompUnit->Cfg);
			rf.run();


#ifdef ENABLE_PFLFRONTEND_DBGFILES
			{
				ofstream ir("cfg_after_opt.dot");
				DumpCFG(ir, false, false);
				ir.close();
			}


#endif

#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running UndoSSA" << std::endl;
#endif
			jit::UndoSSA<PFLCFG> ussa(m_CompUnit->Cfg);
			ussa.run();

#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running Foldcopies" << std::endl;
#endif
			jit::Fold_Copies<PFLCFG> fc(m_CompUnit->Cfg);
			fc.run();

#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running KillRedundant" << std::endl;
#endif
			jit::opt::KillRedundantCopy<PFLCFG> krc(m_CompUnit->Cfg);
			krc.run();

#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running ControlFlow simplification" << std::endl;
#endif
			jit::opt::ControlFlowSimplification<PFLCFG> cfs(m_CompUnit->Cfg);
			cfs.start(changed);


#ifdef _DEBUG_OPTIMIZER
			std::cout << "Running Basic block elimination" << std::endl;
#endif
			jit::opt::BasicBlockElimination<PFLCFG> bbs(m_CompUnit->Cfg);
			bbs.start(changed);

			jit::opt::JumpToJumpElimination<PFLCFG> j2je(m_CompUnit->Cfg);
			jit::opt::EmptyBBElimination<PFLCFG> ebbe(m_CompUnit->Cfg);
			jit::opt::RedundantJumpSimplification<PFLCFG> rds(m_CompUnit->Cfg);

			bool cfs_changed = true;
			while(cfs_changed)
			{
				cfs_changed = false;
				cfs_changed |= ebbe.run();
				bbs.start(changed);
				cfs_changed |= rds.run();
				bbs.start(changed);
				cfs_changed |= j2je.run();
				bbs.start(changed);
			}

		}//}
#ifdef _DEBUG_OPTIMIZER
		std::cout << "Running RegisterMapping" << std::endl;
#endif
		std::set<uint32_t> ign_set;
		jit::Register_Mapping<PFLCFG> rm(m_CompUnit->Cfg, 1, ign_set);
		rm.run();

		m_CompUnit->NumLocals = rm.get_manager().getCount();
		//std::cout << "Il regmanager ha " << rm.get_manager().getCount() << " registri " << std::endl;
		//changed = true;
		//while(changed)
		//{
		//	changed = false;
		//	jit::opt::DeadcodeElimination<PFLCFG> dce_b(m_CompUnit->Cfg);
		//	changed |= dce_b.run();
		//}
		//*******************************************************************
		//*******************************************************************
		//		HERE IT ENDS												*
		//*******************************************************************

#ifdef ENABLE_PFLFRONTEND_DBGFILES
		{
			ofstream irCFG("cfg_ir_opt.dot");
			DumpCFG(irCFG, false, false);
			irCFG.close();

		}
#endif

#ifdef ENABLE_COMPILER_PROFILING
		jit::opt::OptimizerStatistics<PFLCFG> optstats("After PFL");
		std::cout << optstats << std::endl;
#endif


	}
#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksAfter= nbProfilerGetTime();
	printf("\n\n\tMIRO optimized required %ld ticks", (TicksAfter - TicksBefore - MeasureCost));
	TicksBefore= nbProfilerGetTime();
#endif

	ostringstream netIL;
	if (this->m_GlobalSymbols.GetLookupTablesList().size()>0)
		m_CompUnit->UsingCoproLookupEx=true;
	else
		m_CompUnit->UsingCoproLookupEx=false;
		
	if (this->m_GlobalSymbols.GetStringMatchingEntriesCount()>0)
		m_CompUnit->UsingCoproStringMatching=true;
	else
		m_CompUnit->UsingCoproStringMatching=false;
	

	if (this->m_GlobalSymbols.GetRegExEntriesCount()>0)
		m_CompUnit->UsingCoproRegEx=true;
	else
		m_CompUnit->UsingCoproRegEx=false;

	m_CompUnit->DataItems=this->m_GlobalSymbols.GetDataItems();

	m_CompUnit->GenerateNetIL(netIL); //generates the NetIL bytecode

	NetIL_FilterCode = netIL.str();

#ifdef ENABLE_PFLFRONTEND_PROFILING
	TicksAfter= nbProfilerGetTime();
	printf("\n\n\tNetIL generation required %ld ticks\n\n", TicksAfter - TicksBefore - MeasureCost);
#endif

	if (m_GlobalInfo.Debugging.DebugLevel > 1)
		nbPrintDebugLine("Generated NetIL code", DBG_TYPE_INFO, __FILE__, __FUNCTION__, __LINE__, 2);

	m_MIRCodeGen = NULL;
	m_NetVMIRGen = NULL;
	
	return nbSUCCESS;
}


void NetPFLFrontEnd::DumpCFG(ostream &stream, bool graphOnly, bool netIL)
{
	CFGWriter cfgWriter(stream);
	cfgWriter.DumpCFG(m_CompUnit->Cfg, graphOnly, netIL);
}

void NetPFLFrontEnd::DumpFilter(ostream &stream, bool netIL)
{
	if (netIL)
	{
		stream << NetIL_FilterCode;
	}
	else
	{
		CodeWriter cw(stream);
		cw.DumpCode(&m_CompUnit->IRCode);
	}
}

int NetPFLFrontEnd::GenCode(PFLStatement *filterExpression)
{
	#ifdef ENABLE_PFLFRONTEND_PROFILING
		//[icerrato] mesure the time required for the construction of the automaton
		int64_t TicksAutomatonBefore, TicksAutomatonAfter, MeasureCost;
		MeasureCost= nbProfilerGetMeasureCost();
	#endif
	
	stmts = 0;
	SymbolLabel *filterFalseLbl = m_MIRCodeGen->NewLabel(LBL_CODE, "DISCARD");
	SymbolLabel *sendPktLbl = m_MIRCodeGen->NewLabel(LBL_CODE, "SEND_PACKET");
	SymbolLabel *actionLbl = m_MIRCodeGen->NewLabel(LBL_CODE, "ACTION");

	// [ds] created to bypass the if inversion bug
	SymbolLabel *filterExitLbl = m_MIRCodeGen->NewLabel(LBL_CODE, "EXIT");
	
	//here we set the protocol encapsulation graph to the EncapFSA class
	
	//verify if we are doing field extraction
	bool fieldExtraction = false;
	NodeList_t toExtract;
	PFLAction *action = filterExpression->GetAction();
	nbASSERT(action, "action cannot be null")
	if (action->GetType() == PFL_EXTRACT_FIELDS_ACTION)
	{
   		fieldExtraction = true;
   		PFLExtractFldsAction *exFldAct = (PFLExtractFldsAction*) action;
		FieldsList_t &fields = exFldAct->GetFields();
		FieldsList_t::iterator f = fields.begin();

		EncapGraph &protograph = m_GlobalInfo.GetProtoGraph();
		
		EncapFSA::SetGraph(&protograph);

		for (; f != fields.end(); f++)
		{
			SymbolField *field = *f;
			toExtract.push_back(protograph.GetNode(field->Protocol));
			toExtract.sort();
			toExtract.unique();
		}		
   	}

	m_StartProtoGenerated = false;

	#ifdef ENABLE_PFLFRONTEND_PROFILING
		TicksAutomatonBefore = nbProfilerGetTime();
	#endif

	EncapFSA *fsa = VisitFilterExpression(filterExpression->GetExpression(),fieldExtraction,toExtract);
	if(fsa==NULL)
		return nbFAILURE; //an error occurred during the creation of the automaton
		
    PRINT_DOT(fsa,"complete filter expression","completeExpr");
            
    //[icerrato] now, we have to verify of the field extraction is needed...
    if (fieldExtraction)
   	{   	
		EncapFSA *fsaExtractFields = GenFieldExtract(toExtract, sendPktLbl, sendPktLbl, fsa); 
		if(fsaExtractFields==NULL) //an error occurrend during the creation of the automaton
			return nbFAILURE;
					
        PRINT_DOT(fsa,"FSA for the FieldExtraction","filter_fieldextract");
        fsa = fsaExtractFields;
	}   
	
	/* If you need to apply further optimizations, please
	* do it before this call, maybe inside the
	* VisitFilterExpression call chain. This method must
	* be called immediately before the code generation.
	*/
    fsa->fixStateProtocols();

	#ifdef ENABLE_PFLFRONTEND_PROFILING
		TicksAutomatonAfter= nbProfilerGetTime();
		printf("\n\tThe generation of the atuomaton used %ld ticks", TicksAutomatonAfter - TicksAutomatonBefore - MeasureCost);
	#endif

    PRINT_DOT(fsa,"complete and fixed filter expression","completeExprFixed");
    
    //we have to initialize the variables for the header indexing and the tunneled
	GenHeaderIndexingCode(fsa);  
	GenHeaderTunneledCode(fsa); 
    
    m_fsa = fsa;
    
	GenFSACode(fsa, actionLbl, filterFalseLbl, fieldExtraction); 

	m_MIRCodeGen->LabelStatement(actionLbl);
	m_MIRCodeGen->LabelStatement(sendPktLbl);
    // default outport == 1
	m_MIRCodeGen->GenStatement(m_MIRCodeGen->TermNode(SNDPKT, 1));

	// [ds] created to bypass the if inversion bug
	m_MIRCodeGen->JumpStatement(JUMPW, filterExitLbl);

	m_MIRCodeGen->LabelStatement(filterFalseLbl);

	// [ds] created to bypass the if inversion bug	
	m_MIRCodeGen->LabelStatement(filterExitLbl);

	m_MIRCodeGen->GenStatement(m_MIRCodeGen->TermNode(RET));
	//fprintf(stderr, "number of statements: %u\n", stmts);
	return nbSUCCESS;
}

void NetPFLFrontEnd::ReorderStates(list<EncapFSA::State*> *stateList)
{
	list<EncapFSA::State*>::iterator it = stateList->begin();
	it++; //the first state is not startproto
	for(; it != stateList->end(); it++)
	{
		if((*it)->GetInfo()->Name == "startproto")
			break;
	}
	
	nbASSERT(it!=stateList->end(),"Is this automaton without the state related to startprot?");
	list<EncapFSA::State*>::iterator aux = it;
	stateList->erase(it);
	stateList->push_front(*aux);

}

/*
 * If fieldExtractTest != NULL, we're doing field extraction
 * Furthermore, that variable should be set to 1 in the accepting
 * state of the FSA.
 */
void NetPFLFrontEnd::GenFSACode(EncapFSA *fsa, SymbolLabel *trueLabel, SymbolLabel *falseLabel, bool fieldExtraction)
{
	IRLowering NetVMIRGen(*m_CompUnit, *m_MIRCodeGen, falseLabel);
	m_NetVMIRGen = &NetVMIRGen;

	FilterCodeGenInfo protoFilterInfo(*fsa, trueLabel, falseLabel);

	std::list<EncapFSA::State*> stateList = fsa->SortRevPostorder();
	
	std::list<EncapFSA::State*>::iterator i;

    // initialize all the additional state information we need
    sInfoMap_t statesInfo;

    //the first state translated in code must be the initial state, which is related to "startproto"
    if(stateList.size()!=0)
    {
	    if(!((*(stateList.begin()))->GetInfo()->Name == "startproto"))
    		ReorderStates(&stateList);
    }
    
    //creates a label for each state
    for(i = stateList.begin() ; i != stateList.end(); ++i){
          StateCodeInfo *info = new StateCodeInfo(*i);
          SymbolLabel *l_complete = m_MIRCodeGen->NewLabel(LBL_CODE, info->toString() + "_complete");
          SymbolLabel *l_fast = m_MIRCodeGen->NewLabel(LBL_CODE, info->toString() + "_fast");
          info->setLabels(l_complete, l_fast);
          statesInfo.insert( sInfoPair_t(*i, info) ); 
    }
        
    /* Emit a label at the beginning of the FSA code. This will
     * never be reached, but it works around some nasty bugs
     * dealing with empty filters.
     * The bottom line is: "there must always be at least a label in your code".
     */
    m_MIRCodeGen->LabelStatement(m_MIRCodeGen->NewLabel(LBL_CODE, "FSAroot"));
	
    // start parsing the states and generating the code
    // the final not accepting state is not considered
	for(i = stateList.begin() ; i != stateList.end(); i++)
	{
          EncapFSA::State *stateFrom = *i;	
#ifdef OPTIMIZE_SIZED_LOOPS
       SymbolProto *stateProto = stateFrom->GetInfo();

          if(stateProto != NULL){
			 
			m_Protocol = stateProto; //current SymbolProto

			// get referred fields in ExtractField list
			if(m_GlobalSymbols.GetFieldExtractCode(m_Protocol).Front() != NULL)
				this->GetReferredFieldsExtractField(m_Protocol);

			// get referred fields in the protocol code
			this->GetReferredFields( &(m_GlobalSymbols.GetProtoCode(m_Protocol)) );

			// set sized loops to preserve
			for (FieldsList_t::iterator i = m_ReferredFieldsInFilter.begin(); i != m_ReferredFieldsInFilter.end(); i++)
			{
				StmtBase *loop = m_Protocol->Field2SizedLoopMap[*i];

				if (loop != NULL)
				{
					m_Protocol->SizedLoopToPreserve.push_front(loop);

					StmtBase *extLoop = m_Protocol->SizedLoop2SizedLoopMap[loop];
					while (extLoop != NULL)
					{
						m_Protocol->SizedLoopToPreserve.push_front(loop);
						extLoop = m_Protocol->SizedLoop2SizedLoopMap[extLoop];
					}
				}
			}
		
			// set sized loops to preserve
			for (FieldsList_t::iterator i = m_ReferredFieldsInCode.begin(); i != m_ReferredFieldsInCode.end(); i++)
			{
				StmtBase *loop = m_Protocol->Field2SizedLoopMap[*i];

				if (loop != NULL)
				{
					m_Protocol->SizedLoopToPreserve.push_front(loop);

					StmtBase *extLoop = m_Protocol->SizedLoop2SizedLoopMap[loop];
					while (extLoop != NULL)
					{
						m_Protocol->SizedLoopToPreserve.push_front(loop);
						extLoop = m_Protocol->SizedLoop2SizedLoopMap[extLoop];
					}
				}
			}
          }

#endif	
	  //stateFrom is the considered state of the FSA
	  GenProtoCode(stateFrom, protoFilterInfo, statesInfo, fieldExtraction); 
  }
	m_NetVMIRGen = NULL;
	
    m_MIRCodeGen->CommentStatement(""); // spacing
    m_MIRCodeGen->CommentStatement("defaulting the whole filter to FAILURE, if no accepting state was reached");
    m_MIRCodeGen->JumpStatement(JUMPW, protoFilterInfo.FilterFalseLbl);
    m_MIRCodeGen->CommentStatement("FSA implementation ends here");
    m_MIRCodeGen->CommentStatement(""); // spacing
}


EncapFSA *NetPFLFrontEnd::VisitFilterBinExpr(PFLBinaryExpression *expr, bool fieldExtraction, NodeList_t toExtract)
{
	EncapFSA *fsa1 = NULL;
	EncapFSA *fsa2 = NULL;
    EncapFSA *result = NULL;

	switch(expr->GetOperator()) {
	case BINOP_BOOLAND:
            fsa1 = VisitFilterExpression(expr->GetLeftNode(), fieldExtraction, toExtract);
            if(fsa1==NULL)
            	return NULL; //an error occurred during the creation of the automaton
            //nbASSERT(fsa1!=NULL, "FSA SHOULD NOT BE NULL");
          
            fsa2 = VisitFilterExpression(expr->GetRightNode(), fieldExtraction, toExtract);
            if(fsa2==NULL)
            	return NULL; //an error occurred during the creation of the automaton
            //nbASSERT(fsa2!=NULL, "FSA SHOULD NOT BE NULL");
          
            result = EncapFSA::BooleanAND(fsa1,fsa2,fieldExtraction, toExtract);
            break;
          
	case BINOP_BOOLOR:
            fsa1 = VisitFilterExpression(expr->GetLeftNode(), fieldExtraction, toExtract); 
            if(fsa1==NULL)
            	return NULL; //an error occurred during the creation of the automaton
            //nbASSERT(fsa1!=NULL, "FSA SHOULD NOT BE NULL");
            
            fsa2 = VisitFilterExpression(expr->GetRightNode(), fieldExtraction, toExtract);
            if(fsa2==NULL)
            	return NULL; //an error occurred during the creation of the automaton
            //nbASSERT(fsa2!=NULL, "FSA SHOULD NOT BE NULL");

            PRINT_DOT(fsa1,"Before OR: FSA1","before_or_1");
            PRINT_DOT(fsa2,"Before OR: FSA2","before_or_2");

            result = EncapFSA::BooleanOR(fsa1, fsa2, fieldExtraction, toExtract);

            PRINT_DOT(result,"After determinization","after_nfa2dfa");

            break;          

	default:
          nbASSERT(false, "CANNOT BE HERE");
          break;
	}

        PRINT_DOT(result,"Just before returning from VisitFilterBinExpr","visitfilter_end");
	return result;
}


EncapFSA *NetPFLFrontEnd::VisitFilterUnExpr(PFLUnaryExpression *expr, bool fieldExtraction, NodeList_t toExtract)
{
	EncapFSA *fsa = NULL;
	switch(expr->GetOperator())
	{
	case UNOP_BOOLNOT:
		fsa = VisitFilterExpression(expr->GetChild(), fieldExtraction,toExtract);
		if(fsa==NULL)
	            	return NULL; //an error occurred during the creation of the automaton
		fsa->BooleanNot();
		break;
	default:
		nbASSERT(false, "CANNOT BE HERE");
		break;
	}
	return fsa;
}

EncapFSA *NetPFLFrontEnd::VisitFilterRegExpr(PFLRegExpExpression *expr, bool fieldExtraction, NodeList_t toExtract)
{
	EncapFSA *regExpDFA = NULL;
	switch(expr->GetOperator())
	{
		case REGEXP_OP:
		{
				EncapGraph &protoGraph = m_GlobalInfo.GetProtoGraph();
                m_GlobalSymbols.UnlinkProtoLabels();
                protoGraph.RemoveUnconnectedNodes();
                EncapFSA::Alphabet_t fsaAlphabet;
                EncapFSABuilder fsaRegExpBuilder(protoGraph, fsaAlphabet);     
                EncapFSA *regExp = fsaRegExpBuilder.BuildRegExpFSA(expr->GetInnerList());
                if(regExp==NULL)
                {
                	m_ErrorRecorder = fsaRegExpBuilder.GetErrRecorder();
                	//an error occurred and the automaton has not been built
					return NULL;
                }
                				
                regExpDFA = EncapFSA::NFAtoDFA(regExp,fieldExtraction,toExtract,false);
                PRINT_DOT(regExpDFA,"After regexpfsa determinization","regexpfsa_after_nfa2dfa");
                
              	fsaRegExpBuilder.FixAutomaton(regExpDFA);  
             	PRINT_DOT(regExpDFA,"The regexp FSA is complete!","regexpfsa_complete");
           
                return regExpDFA;
			break;
		}
		default:
			nbASSERT(false, "CANNOT BE HERE");
	}
	return regExpDFA;
}


EncapFSA* NetPFLFrontEnd::GenFieldExtract(NodeList_t &protocols, SymbolLabel *trueLabel, SymbolLabel *falseLabel, EncapFSA *fsa)
{
	IRLowering NetVMIRGen(*m_CompUnit, *m_MIRCodeGen, falseLabel);
	m_NetVMIRGen = &NetVMIRGen;

	//Info-Partition Initialization
	for (FieldsList_t::iterator i = m_FieldsList.begin(); i != m_FieldsList.end(); i++)
	{	
		switch((*i)->FieldType)
		{
			case PDL_FIELD_ALLFIELDS:
				{
					//[icerrato] allfields can be specified only on the last protocol of the list. So we must check if this is true in the filter
					FieldsList_t::iterator aux = i;
					aux++;
					if(aux!=m_FieldsList.end())
					{
						//there is an error in the filter
						m_ErrorRecorder.PFLError("You can specify \"allfields\" only as last element of the filter, e.g. \"extracfields(proto1.x,proto2.allfields)\"");
						return NULL;
					}
					m_MIRCodeGen->GenStatement(m_MIRCodeGen->BinOp(IISTR, m_MIRCodeGen->TermNode(PUSH,(uint32)0), m_MIRCodeGen->TermNode(PUSH,(uint32)(*i)->Position)));
				}
			break;

			case PDL_FIELD_BITFIELD:
				m_MIRCodeGen->GenStatement(m_MIRCodeGen->BinOp(IISTR, m_MIRCodeGen->TermNode(PUSH,(uint32)0), m_MIRCodeGen->TermNode(PUSH,(uint32)(*i)->Position)));
			break;
			
			case PDL_FIELD_FIXED:
				if((*i)->MultiProto)
					m_MIRCodeGen->GenStatement(m_MIRCodeGen->BinOp(IISTR, m_MIRCodeGen->TermNode(PUSH,(uint32)0), m_MIRCodeGen->TermNode(PUSH,(uint32)(*i)->Position)));
				else if((*i)->MultiField)
					m_MIRCodeGen->GenStatement(m_MIRCodeGen->BinOp(IISTR, m_MIRCodeGen->TermNode(PUSH,(uint32)0), m_MIRCodeGen->TermNode(PUSH,(uint32)(*i)->Position)));
				else
					m_MIRCodeGen->GenStatement(m_MIRCodeGen->BinOp(IISTR, m_MIRCodeGen->TermNode(PUSH,(uint32)0), m_MIRCodeGen->TermNode(PUSH,(uint32)((*i)->Position/*+2*/))));//[icerrato] +2 should be wrong

			break;

			default:
				m_MIRCodeGen->GenStatement(m_MIRCodeGen->BinOp(ISSTR, m_MIRCodeGen->TermNode(PUSH,(uint32)0), m_MIRCodeGen->TermNode(PUSH,(uint32)((*i)->Position/*+2*/))));//[icerrato] +2 should be wrong
			break;
		}	
	}
	
    EncapGraph &protoGraph = m_GlobalInfo.GetProtoGraph();

    m_GlobalSymbols.UnlinkProtoLabels();

    protoGraph.RemoveUnconnectedNodes();

    EncapFSA::Alphabet_t fsaAlphabet;

    EncapFSABuilder fsaBuilder(protoGraph, fsaAlphabet);

    EncapFSA *fsaExtrField = fsaBuilder.ExtractFieldsFSA(protocols, fsa);
    nbASSERT(fsa!=NULL, "FSA SHOULD NOT BE NULL");

    return fsaExtrField;
}

//Creates an fsa with only the startproto in case of there is the exctractfield action but the filtering expression has not been specified
EncapFSA *NetPFLFrontEnd::VisitNullExpr() //[icerrato]
{
	EncapGraph &protoGraph = m_GlobalInfo.GetProtoGraph();
	EncapFSA::Alphabet_t fsaAlphabet;
	EncapFSABuilder fsaBuilder(protoGraph, fsaAlphabet);
	return fsaBuilder.BuildSingleNodeFSA();
}

EncapFSA *NetPFLFrontEnd::VisitFilterExpression(PFLExpression *expr, bool fieldExtraction, NodeList_t toExtract)
{
	EncapFSA *fsa = NULL;
	
	if(expr != NULL)
	{
		switch(expr->GetType())
		{
		case PFL_BINARY_EXPRESSION:
			fsa = VisitFilterBinExpr((PFLBinaryExpression*)expr, fieldExtraction, toExtract);
			break;
		case PFL_UNARY_EXPRESSION:
			fsa = VisitFilterUnExpr((PFLUnaryExpression*)expr, fieldExtraction, toExtract);
			break;
		case PFL_REGEXP_EXPRESSION:
			fsa = VisitFilterRegExpr((PFLRegExpExpression*)expr, fieldExtraction, toExtract);
			break;
		case PFL_TERM_EXPRESSION:
			nbASSERT(false,"You cannot be here! Also if the filter is a single protocol, this protocol should be into a PFL_REGEXP_EXPRESSION!");
			break;
		default:
			nbASSERT(false,"You cannot be here!");
		}
	}
	else
		//this happens if the filter has not been specified. However an automaton is created also in this case, and it has the state related to the startproto which is also the final accepting state of the FSA
		fsa = VisitNullExpr();
	return fsa;
}


void NetPFLFrontEnd::ParseExtractField(FieldsList_t *fldList)
{
	uint32 count=0;
	
	for(FieldsList_t::iterator i = fldList->begin(); i!= fldList->end();i++)
	{
		/* ALLFIELDS */
	
		if((*i)->FieldType==PDL_FIELD_ALLFIELDS)
		{	
			(*i)->Position=count; //[icerrato]
			StmtFieldInfo *fldInfoStmt= new StmtFieldInfo((*i),count);
			m_GlobalSymbols.AddFieldExtractStatment((*i)->Protocol,fldInfoStmt);
			(*i)->Protocol->ExAllfields=true;
			(*i)->Protocol->position= m_MIRCodeGen->NewTemp(string("allfields_position"), m_CompUnit->NumLocals);
			(*i)->Protocol->beginPosition=count;
			this->m_MIRCodeGen->CommentStatement(string("Initializing allfields position and counter"));
			this->m_MIRCodeGen->LabelStatement(new SymbolLabel(LBL_CODE, 0, string("init_allfields")));
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(LOCST,m_MIRCodeGen->TermNode(PUSH,count),(*i)->Protocol->position));
			(*i)->Protocol->NExFields= m_MIRCodeGen->NewTemp(string("allfields_count"), m_CompUnit->NumLocals);
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(LOCST,m_MIRCodeGen->TermNode(PUSH,(uint32)0),(*i)->Protocol->NExFields));
		}
		SymbolField *fld=m_GlobalSymbols.LookUpProtoField((*i)->Protocol,(*i));
		StmtFieldInfo *fldInfoStmt= new StmtFieldInfo(fld,count);
		m_GlobalSymbols.AddFieldExtractStatment((*i)->Protocol,fldInfoStmt);
		fld->ToExtract=true;
		fld->Position=count;
		
		/*************************************************************************************************************************************************************************/
		/* VARIABLES DEFINITION */
		
		if(fld->Protocol->ExtractG == NULL)
		{
			//[icerrato] proto_ExtractG: counts the number of time an header related to proto has been found into the packet
			//note that each protocol involved in field extraction has this variable
			fld->Protocol->ExtractG = m_MIRCodeGen->NewTemp(fld->Protocol->Name + string("_ExtractG"), m_CompUnit->NumLocals);
			this->m_MIRCodeGen->CommentStatement(string("Initializing Header Counter for protocol ") + fld->Protocol->Name);
			this->m_MIRCodeGen->LabelStatement(new SymbolLabel(LBL_CODE, 0, string("init_") + fld->Protocol->Name + string("_ExtractG")));
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(LOCST,m_MIRCodeGen->TermNode(PUSH,(uint32)0), fld->Protocol->ExtractG)); //proto_ExtractG = 0
		}

		if ((*i)->FieldType!=PDL_FIELD_ALLFIELDS)
		{
			//nbASSERT((*i)->FieldType!=PDL_FIELD_ALLFIELDS,"There is a bug. This situation should be avoided by the grammar.");//[icerrato]
			fld->FieldCount = m_MIRCodeGen->NewTemp(fld->Name + string("_counter"), m_CompUnit->NumLocals);
			this->m_MIRCodeGen->CommentStatement(string("Initializing Field Counter for field ") + fld->Name);
			this->m_MIRCodeGen->LabelStatement(new SymbolLabel(LBL_CODE, 0, string("init_") + fld->Protocol->Name + string("_") + fld->Name + string("_Counter")));
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(LOCST,m_MIRCodeGen->TermNode(PUSH,(uint32)0), fld->FieldCount)); //field_counter = 0
		
			fld->Protocol->fields_ToExtract.push_back(fld);
		}
		
		/****************************************************************************************************************************************************/
		/* OTHER OPERATIONS */
		
		if (fld->MultiProto || fld->MultiField)
		{
			count += nbNETPFLCOMPILER_INFO_FIELDS_SIZE * (1 + nbNETPFLCOMPILER_MAX_PROTO_INSTANCES);
		}
		else
		{
			count += nbNETPFLCOMPILER_INFO_FIELDS_SIZE;
		}
	}
}

void NetPFLFrontEnd::VisitAction(PFLAction *action)
{
	switch(action->GetType())
	{
	case PFL_EXTRACT_FIELDS_ACTION:
		{
		PFLExtractFldsAction * ExFldAction = (PFLExtractFldsAction *)action;
		m_FieldsList = ExFldAction->GetFields();
		ParseExtractField(&m_FieldsList);
		}
		break;
	case PFL_CLASSIFY_ACTION:
		//not yet implemented
		break;
	case PFL_RETURN_PACKET_ACTION:
		break;
	}
}

//initialize the variables for the header indexing
void NetPFLFrontEnd::GenHeaderIndexingCode(EncapFSA *fsa)
{
	this->m_MIRCodeGen->CommentStatement(string("Initializing counters for header indexing"));
	EncapFSA::Code_t code = fsa->m_code1;
	for(EncapFSA::Code_t::iterator c = code.begin(); c != code.end(); c++)
	{
		this->m_MIRCodeGen->LabelStatement(new SymbolLabel(LBL_CODE, 0, string("init_") + ((*c).first)->ToString() + string("_indexing")));
		SymbolTemp *var = m_MIRCodeGen->NewTemp(string("indexing_")+((*c).first)->ToString(), m_CompUnit->NumLocals); //create a new variable...
		nbASSERT(var->SymKind==SYM_TEMP,"There is a bug!");
		m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(LOCST,m_MIRCodeGen->TermNode(PUSH,(uint32)0), var)); //... and initialize it to zero
		EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, (*c).first);
		fsa->AddVariable1(label,var);//add the variable to the automaton
	}
}	

//initialize the variables for the tunneled
//if we have for example "ip tunneled", we create the code to increment the variable for IP and IPv6, 
//since both the protocols have level 3.
//but we create a single variable for level 3.
void NetPFLFrontEnd::GenHeaderTunneledCode(EncapFSA *fsa)
{

	this->m_MIRCodeGen->CommentStatement(string("Initializing counters for tunneled"));
	map<double,SymbolTemp*> layers;
	EncapFSA::Code_t code = fsa->m_code2;
	for(EncapFSA::Code_t::iterator c = code.begin(); c != code.end(); c++)
	{
		if(layers.count(((*c).first)->Level)==0)
		{
			stringstream s;
			s << ((*c).first)->Level;
			this->m_MIRCodeGen->LabelStatement(new SymbolLabel(LBL_CODE, 0, string("init_") + s.str() + string("_tunneled")));
			SymbolTemp *var = m_MIRCodeGen->NewTemp(string("tunneled_level")+/*((*c).first)->ToString()*/s.str(), m_CompUnit->NumLocals); //create a new variable...
			nbASSERT(var->SymKind==SYM_TEMP,"There is a bug!");
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(LOCST,m_MIRCodeGen->TermNode(PUSH,(uint32)0), var)); //... and initialize it to zero
			EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, (*c).first);
			fsa->AddVariable2(label,var);//add the variable to the automaton
			layers[((*c).first)->Level] = var;
		}
		else
		{
			//the variable has already been created
			map<double,SymbolTemp*>::iterator exist = layers.find(((*c).first)->Level);
			EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, (*c).first);
			fsa->AddVariable2(label,(*exist).second);//add the variable to the automaton
		}
	}
}	

void NetPFLFrontEnd::GenStringMatchingEntries(void)
{
	
	int smEntries=this->m_GlobalSymbols.GetStringMatchingEntriesCount();
	
	if (smEntries>0)
	{
		int index = 0;
		char bufEntriesCount[10];
		char bufIndex[10];
		char sensitiveStr[10];
		char bufPatLen[10];

		//itoa(regexEntries, bufEntriesCount, 10);
		snprintf(bufEntriesCount, 9, "%d", smEntries);

		SymbolDataItem *smData = new SymbolDataItem(string("stringmatching_data"), DATA_TYPE_WORD, string(bufEntriesCount), index);
		this->m_GlobalSymbols.StoreDataItem(smData);

		RegExList_t list = this->m_GlobalSymbols.GetStringMatchingList();

		index++;
		for (RegExList_t::iterator i=list.begin(); i!=list.end(); i++, index++)
		{
			snprintf(bufIndex, 9, "%d", index);

			int sensitive=( (*i)->CaseSensitive? 0 : 1 );
			//int optLenValue=( (*i)->CaseSensitive? 2 : 3 );
			snprintf(sensitiveStr, 9, "%d", sensitive);

			//int escapes=0;
			int slashes=0;
			int length=(*i)->Pattern->Size;
			for (int cIndex=0; cIndex<length; cIndex++)
			{
				if ((*i)->Pattern->Name[cIndex]=='\\')
				{
					slashes++;
				}
			}

			length-=slashes/2;
			snprintf(bufPatLen, 9, "%d", length);
		
			SymbolDataItem *pathNo = new SymbolDataItem(string("path_no_").append(bufIndex), DATA_TYPE_WORD, string("1"), index);
			this->m_GlobalSymbols.StoreDataItem(pathNo); 		
				
			SymbolDataItem *pathLen = new SymbolDataItem(string("path_length_").append(bufIndex), DATA_TYPE_WORD, string(bufPatLen), index);
			this->m_GlobalSymbols.StoreDataItem(pathLen);
			
			SymbolDataItem *caseSensitive = new SymbolDataItem(string("case_").append(bufIndex), DATA_TYPE_WORD,sensitiveStr, index);
			this->m_GlobalSymbols.StoreDataItem(caseSensitive);
			
			SymbolDataItem *dataRet = new SymbolDataItem(string("data_").append(bufIndex), DATA_TYPE_DOUBLE, string("0"), index);
			this->m_GlobalSymbols.StoreDataItem(dataRet);
			
			SymbolDataItem *pattern = new SymbolDataItem(string("pattern_").append(bufIndex), DATA_TYPE_BYTE, string("\"").append(string((*i)->Pattern->Name)).append("\""), index);
			this->m_GlobalSymbols.StoreDataItem(pattern);
			
		}
	}
}

void NetPFLFrontEnd::GenRegexEntries(void)
{

	int regexEntries=this->m_GlobalSymbols.GetRegExEntriesCount();
	if (regexEntries>0)
	{
		int index = 0;
		char bufEntriesCount[10];
		char bufIndex[10];
		char bufOptLen[10];
		char bufOpt[10]={0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		char bufPatLen[10];

		//itoa(regexEntries, bufEntriesCount, 10);
		snprintf(bufEntriesCount, 9, "%d", regexEntries);

		SymbolDataItem *regexData = new SymbolDataItem(string("regexp_data"), DATA_TYPE_WORD, string(bufEntriesCount), index);
		//m_CodeGen->GenStatement(m_CodeGen->TermNode(IR_DATA, regexData));
		this->m_GlobalSymbols.StoreDataItem(regexData);

		RegExList_t list = this->m_GlobalSymbols.GetRegExList();

		index++;
		for (RegExList_t::iterator i=list.begin(); i!=list.end(); i++, index++)
		{
			snprintf(bufIndex, 9, "%d", index);

			int optLenValue=1;
			snprintf(bufOptLen, 9, "%d", optLenValue);

			//int escapes=0;
			int slashes=0;
			int length=(*i)->Pattern->Size;
			for (int cIndex=0; cIndex<length; cIndex++)
			{
				if ((*i)->Pattern->Name[cIndex]=='\\')
				{
					slashes++;
				}
			}

			length-=slashes/2;
			snprintf(bufPatLen, 9, "%d", length);

			if ((*i)->CaseSensitive)
				strncpy(bufOpt, "s", 1);
			else
				strncpy(bufOpt, "i", 1);

			SymbolDataItem *optLen = new SymbolDataItem(string("opt_len_").append(bufIndex), DATA_TYPE_WORD, string(bufOptLen), index);
			this->m_GlobalSymbols.StoreDataItem(optLen);

			SymbolDataItem *opt = new SymbolDataItem(string("opt_").append(bufIndex), DATA_TYPE_BYTE, string("\"").append(string(bufOpt)).append("\""), index);
			this->m_GlobalSymbols.StoreDataItem(opt);

			SymbolDataItem *patternLen = new SymbolDataItem(string("pat_len_").append(bufIndex), DATA_TYPE_WORD, string(bufPatLen), index);
			this->m_GlobalSymbols.StoreDataItem(patternLen);

			SymbolDataItem *pattern = new SymbolDataItem(string("pat_").append(bufIndex), DATA_TYPE_BYTE, string("\"").append(string((*i)->Pattern->Name)).append("\""), index);
			this->m_GlobalSymbols.StoreDataItem(pattern);
		}
	}
}


//this method generates initialization MIR code for the lookup_ex end regex coprocessors (if it is needed)
void NetPFLFrontEnd::GenInitCode()
{
	this->m_MIRCodeGen->CommentStatement(string("INITIALIZATION"));
	this->m_MIRCodeGen->LabelStatement(new SymbolLabel(LBL_CODE, 0, string("init")));

	if (this->m_GlobalSymbols.GetRegExEntriesCount()>0)
	{//the regex coprocessor is needed
		this->m_MIRCodeGen->CommentStatement(string("Initialize Regular Expression coprocessor"));
		m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(POP,
			m_MIRCodeGen->UnOp(COPINIT, new SymbolLabel(LBL_ID, 0, "regexp"), 0, new SymbolLabel(LBL_ID, 0, "regexp_data"))));
	}

	if (this->m_GlobalSymbols.GetStringMatchingEntriesCount()>0)
	{//the stringmatching coprocessor is needed
		this->m_MIRCodeGen->CommentStatement(string("Initialize String Matching coprocessor"));
		m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(POP,
			m_MIRCodeGen->UnOp(COPINIT, new SymbolLabel(LBL_ID, 0, "stringmatching"), 0, new SymbolLabel(LBL_ID, 0, "stringmatching_data"))));
	}

	LookupTablesList_t lookupTablesList = this->m_GlobalSymbols.GetLookupTablesList();
	m_MIRCodeGen->CommentStatement(string("Initialize Lookup_ex coprocessor"));
	if (lookupTablesList.size() > 0)
	{//the lookup_ex coprocessor is needed
		m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(COPOUT,
			m_MIRCodeGen->TermNode(PUSH, lookupTablesList.size()),
			new SymbolLabel(LBL_ID, 0, "lookup_ex"), LOOKUP_EX_OUT_TABLE_ID));

		m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(COPRUN, new SymbolLabel(LBL_ID, 0, "lookup_ex"), LOOKUP_EX_OP_INIT));

		// initialize lookup coprocessor
		for (LookupTablesList_t::iterator i = lookupTablesList.begin(); i!=lookupTablesList.end(); i++)
		{
			SymbolLookupTable *table = (*i);

			m_MIRCodeGen->CommentStatement(string("Initialize the table ").append(table->Name));

			// set table id
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(COPOUT,
				m_MIRCodeGen->TermNode(PUSH, table->Id),
				table->Label,
				LOOKUP_EX_OUT_TABLE_ID));

			// set entries number
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(COPOUT,
				m_MIRCodeGen->TermNode(PUSH, table->MaxExactEntriesNr+table->MaxMaskedEntriesNr),
				table->Label,
				LOOKUP_EX_OUT_ENTRIES));


			// set data size
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(COPOUT,
				m_MIRCodeGen->TermNode(PUSH, table->KeysListSize),
				table->Label,
				LOOKUP_EX_OUT_KEYS_SIZE));

			// set value size
			uint32 valueSize = table->ValuesListSize;
			if (table->Validity==TABLE_VALIDITY_DYNAMIC)
			{
				// hidden values
				for (int i=0; i<HIDDEN_LAST_INDEX; i++)
					valueSize += (table->HiddenValuesList[i]->Size/sizeof(uint32));
			}
			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(COPOUT,
				m_MIRCodeGen->TermNode(PUSH, valueSize),
				table->Label,
				LOOKUP_EX_OUT_VALUES_SIZE));

			m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(COPRUN, table->Label, LOOKUP_EX_OP_INIT_TABLE));
		}
	}

}


/*
 * If fieldExtraction == true, we're doing field extraction
 */
void NetPFLFrontEnd::GenProtoCode(EncapFSA::State *stateFrom, //current state
                                  FilterCodeGenInfo &protoFilterInfo,
                                  const sInfoMap_t statesInfo,
                                  bool fieldExtraction)
{	
  // safety check
  if (stateFrom->isFinal())
    nbASSERT(stateFrom->isAccepting(), "A final, non-accepting state should not have got so far.");

  sInfoMap_t::const_iterator infoPtr = statesInfo.find(stateFrom);
  nbASSERT(infoPtr!=statesInfo.end(), "The info about me should be already initialized");
 
  StateCodeInfo *curStateInfo = infoPtr->second;
  EncapFSA *fsa = &(protoFilterInfo.fsa);
  
  // 1) emit the "complete" label
  m_MIRCodeGen->CommentStatement(""); // spacing
  m_MIRCodeGen->LabelStatement(curStateInfo->getLabelComplete());


  // 2) emit the before section for the protocol, if it is the only one of the current state
  if(stateFrom->GetInfo()!=NULL){ 
    GenerateBeforeCode( stateFrom->GetInfo() );
  }

  // 3) emit the format section, in case of this state is not final (or we are doing field extraction) and it is related to a single protocol. Also the information about the field extraction are issued here
  if( (stateFrom->GetInfo()!=NULL) && (!stateFrom->isFinal() || fieldExtraction))
  {
	m_NetVMIRGen->LowerHIRCode(&(m_GlobalSymbols.GetProtoCode(stateFrom->GetInfo())),
				                   stateFrom->GetInfo(),
				                   "emitting format for proto inside " + curStateInfo->toString());
	m_MIRCodeGen->CommentStatement("DONE emitting the proto format");

  }

  // 4) emit the middle ("fast") label
  m_MIRCodeGen->LabelStatement(curStateInfo->getLabelFast());
  
  // 5) if we are in a final accepting state, we're done. Emit a jump and return
  if(stateFrom->isFinal())
  {
    m_MIRCodeGen->JumpStatement(JUMPW, protoFilterInfo.FilterTrueLbl );
    return;
  }
  
  // 6) cycle on all transitions and prepare the data structures
  std::map<string, SymbolLabel*> toStateMap; 									/* map of the states reachable from this one
  																				 */
  std::map<EncapFSA::ExtendedTransition*, SymbolLabel*> et_tohandle;		    /* map of the ET that must be handled. The mapped value is a
																				  temporary label at which position all the code for the ET will be
																				  generated.*/
  std::map<EncapFSA::ExtendedTransition*, bool> endFinal;						/* for each ET that must be handled, this map says if at least one 
  																				   of the destination state is final or not*/
  std::map<string, EarlyCodeEmission_t> early_protos;							/* map of protocols for which we need to emit early the before code
																				 * (needed when the destination state does not identify a unique
																				 * protocol, but the before code for it must be emitted)
																				 */	
   std::map<EncapLabel_t,SymbolLabel*> symbols_code_tohandle;					/* map of the symbols having code that must be executed before the reaching of the next state */
  std::map<EncapLabel_t,SymbolLabel*> after_symbols_code;						/* map of the labels to jump after the handling of the code related to a symlo*/
  
  std::list<EncapFSA::Transition*> trlist = fsa->getTransExitingFrom(stateFrom);

  	for (std::list<EncapFSA::Transition*>::iterator tr_iter = trlist.begin();tr_iter != trlist.end();++tr_iter) 
	{  
      
    // GetInfo() returns a trans_pair_t containing a Alphabet_t and a PType object
    trans_pair_t trinfo = (*tr_iter)->GetInfo();
    
    if((*tr_iter)->getIncludingET() == NULL) { 
      // if the transition has no predicate, then simply set the target state as the destination of the jump

      sInfoMap_t::const_iterator to_s = statesInfo.find(&* ((*tr_iter)->ToState()) );
      if (to_s == statesInfo.end()) {

        // jump to fail
        if ((*tr_iter)->IsComplementSet())
        	toStateMap[LAST_RESORT_JUMP] = protoFilterInfo.FilterFalseLbl;
        else
          for(EncapFSA::Alphabet_t::iterator sym = trinfo.first.begin();sym != trinfo.first.end();++sym)
          toStateMap[(*sym).second->Name] = protoFilterInfo.FilterFalseLbl;

      } 
      else {

        if ((*tr_iter)->IsComplementSet())
          toStateMap[LAST_RESORT_JUMP] = (*to_s).second->getLabelComplete();
        else
          for(EncapFSA::Alphabet_t::iterator sym = trinfo.first.begin();
            sym != trinfo.first.end();
            ++sym)
          {
            // for each symbol, check if we can directly jump to the destination state,
            // or if we need to emit early its before (and eventually its format and encapsulation) code
          	if( to_s->first->GetInfo() == NULL && (  (*sym).second->FirstExecuteBefore || (fieldExtraction))) //[icerrato]
            {
            	nbASSERT(false,"Is there a BUG? I'm not sure but I think so");
 	           	toStateMap[(*sym).second->Name] = setupEarlyCode(&early_protos,(*sym).second,(*to_s).second->getLabelComplete(),fieldExtraction);
    	    }
            else
           	{
           		if(m_fsa->HasCode(*sym))
           		{
	           		//we have to manage the counters for the header indexing and/or the tunneled
	           		SymbolLabel *tmp_label = m_MIRCodeGen->NewLabel(LBL_CODE, "symbol_code_tmp");
	           		symbols_code_tohandle[*sym] = tmp_label;
	           		after_symbols_code[*sym] = (*to_s).second->getLabelComplete();       
	           		toStateMap[(*sym).second->Name] = tmp_label;
	           	}           		
           		else
	              	toStateMap[(*sym).second->Name] = (*to_s).second->getLabelComplete();          
            }
          }

      }
    }	//end ((*tr_iter)->getIncludingET() == NULL)
    else {
      // otherwise, handle the predicate

      // sanity checks
      nbASSERT(!(*tr_iter)->IsComplementSet(), "A predicate on a complemented-set transition?");
      nbASSERT(trinfo.first.size()==1, "A predicate on a transition with more than one symbol?");
      EncapLabel_t label = *(trinfo.first.begin());
      SymbolProto *proto = label.second;

      // extract the ExtendedTransition that handles this predicate
      EncapFSA::ExtendedTransition *et = (*tr_iter)->getIncludingET();

      // see if it has been already scheduled for post-inspection...
      std::map<EncapFSA::ExtendedTransition*, SymbolLabel*>::iterator et_check = et_tohandle.find(et);
      //..if not, do it
      if (et_check == et_tohandle.end()){
      	//it is possible that, before the execution of the code of the ET, we have to execute the code related to the symbol
      	bool hasCode = m_fsa->HasCode(label);
      	if(hasCode)
      	{
      		SymbolLabel *tmp_label = m_MIRCodeGen->NewLabel(LBL_CODE, "symbol_code_tmp");
	        symbols_code_tohandle[label] = tmp_label;
	        toStateMap[(label).second->Name] = tmp_label;
      	}
      
        SymbolLabel *tmp_label = m_MIRCodeGen->NewLabel(LBL_CODE, "ET_tmp");
        et_tohandle.insert(std::pair<EncapFSA::ExtendedTransition*, SymbolLabel*>(et,tmp_label));
        toStateMap.insert(std::pair<string,SymbolLabel*>(proto->Name,tmp_label));
        
        if(hasCode)
        	after_symbols_code[label] = tmp_label;       
        
      }
      EncapStIterator_t dest = (*tr_iter)->ToState();
   	  if((&(*dest))->isAccepting())
      {
      	endFinal.insert(pair<EncapFSA::ExtendedTransition*,bool>(et,true));
      }
    }
  }
  // ended scanning the outgoing transitions


  // 6) parse and emit the encapsulation section, if the current state is related to a single protocol
  if(stateFrom->GetInfo()!=NULL)
  {
	  CodeList *encapCode = m_GlobalSymbols.NewCodeList(true);
	  HIRCodeGen encapCodeGen(m_GlobalSymbols, encapCode);
	  NetPDLParser.ParseEncapsulation(stateFrom->GetInfo(), toStateMap, fsa, stateFrom->isAccepting(),
		                              protoFilterInfo.FilterFalseLbl, protoFilterInfo.FilterTrueLbl, encapCodeGen);
	  // emit the encapsulation code
	  m_NetVMIRGen->LowerHIRCode(encapCode, "emitting encapsulation for proto inside " + curStateInfo->toString());
	  m_MIRCodeGen->CommentStatement("DONE emitting the proto encapsulation");
   }


  // 7) emit the symbol code
  if(!symbols_code_tohandle.empty())
  {
	m_MIRCodeGen->CommentStatement("BEGIN handling symbols code");
	std::map<EncapLabel_t, SymbolLabel*>:: iterator c = symbols_code_tohandle.begin();
	std::map<EncapLabel_t, SymbolLabel*>:: iterator d = after_symbols_code.begin();
  	for (; c != symbols_code_tohandle.end();++c,++d) 
  	{
  		m_MIRCodeGen->LabelStatement(c->second); // generate the proper temporary label
  		GenerateSymbolsCode(c->first,d->second);
  	} 
  	m_MIRCodeGen->CommentStatement("DONE handling symbols code"); 
  }


  // 8) emit the ExtendedTransition code
  if (!et_tohandle.empty()) {
    m_MIRCodeGen->CommentStatement("BEGIN handling ExtendedTransitions");
    for (std::map<EncapFSA::ExtendedTransition*, SymbolLabel*>:: iterator et_iter = et_tohandle.begin();
         et_iter != et_tohandle.end();
         ++et_iter) {
      m_MIRCodeGen->LabelStatement(et_iter->second); // generate the proper temporary label
      CodeList *ETcode = m_GlobalSymbols.NewCodeList(true);
      HIRCodeGen ETcodeGen(m_GlobalSymbols, ETcode);
      EncapFSA::Alphabet_t alphEt = (*et_iter).first->GetLabels();
      nbASSERT(alphEt.size() == 1,"An exended transition cannot have more than one symbol");
      GenerateETCode(et_iter->first, statesInfo, protoFilterInfo.FilterFalseLbl, &ETcodeGen, fieldExtraction || !(*(endFinal.find((*et_iter).first))).second,*(alphEt.begin()));
      m_NetVMIRGen->LowerHIRCode(ETcode, "");
    }
    m_MIRCodeGen->CommentStatement("DONE handling ExtendedTransitions");
  }
  
  // 9) emit the early code for those states that need it
  m_MIRCodeGen->CommentStatement("");
//  m_MIRCodeGen->CommentStatement("BEGIN early code emission");
  for(std::map<string, EarlyCodeEmission_t>::iterator i = early_protos.begin();
      i != early_protos.end();
      ++i)
  {
  	nbASSERT(false,"Is there a BUG? I'm not sure but I think so");
    // emit the start label
    m_MIRCodeGen->LabelStatement(i->second.start_lbl);
    SymbolProto *protocol = i->second.protocol;
    m_MIRCodeGen->CommentStatement("EARLY BEFORE SECTION FOR PROTOCOL: " +protocol->Name);
    GenerateBeforeCode(protocol);
    m_MIRCodeGen->JumpStatement(JUMPW, i->second.where_to_jump_after);
  }
  //m_MIRCodeGen->CommentStatement("DONE early code emission");
 
  return;
}

SymbolLabel* NetPFLFrontEnd::setupEarlyCode(std::map<string, EarlyCodeEmission_t>* m,
                                            SymbolProto *p,
                                            SymbolLabel *jump_after,
                                            bool needFormat)
{
  std::map<string, EarlyCodeEmission_t>::iterator i = m->find(p->Name);
  if(i != m->end())
    return i->second.start_lbl;

  struct EarlyCodeEmission_t code;  
  code.start_lbl = (needFormat)? m_MIRCodeGen->NewLabel(LBL_CODE, p->Name + "_earlybefore_and_earlyformat") : m_MIRCodeGen->NewLabel(LBL_CODE, p->Name + "_earlybefore");
  code.protocol = p;
  code.where_to_jump_after = jump_after;

  (*m)[p->Name] = code;
  return code.start_lbl;
}

void NetPFLFrontEnd::GenerateSymbolsCode(EncapLabel_t symbol, SymbolLabel* destination)
{
	
	list<SymbolTemp*> variables = m_fsa->GetVariables(symbol);
	nbASSERT(variables.size()!=0,"There is a BUG! This symbol has code and has not a variable O_o");
	nbASSERT(destination!=NULL,"There is a bug! The lable cannot be NULL");
	
	//increment each variable - (*it)++
	for(list<SymbolTemp*>::iterator it = variables.begin(); it != variables.end(); it++)		
		m_MIRCodeGen->GenStatement(m_MIRCodeGen->UnOp(LOCST, m_MIRCodeGen->BinOp(ADD, m_MIRCodeGen->TermNode(LOCLD, (*it)), m_MIRCodeGen->TermNode(PUSH, 1)),(*it)));
	
	//jump to the next code to execute
	m_MIRCodeGen->JumpStatement(JUMPW,destination);

}

//resetOffset is true in case of, after the parsing of the format of the destination protocol, the offset into the packet must be set before the format.
//this is needed in case of the destination state of the et is not the final accepting state
void NetPFLFrontEnd::GenerateETCode(EncapFSA::ExtendedTransition *et,
                                    const sInfoMap_t stateMappings,
                                    SymbolLabel *defaultFailure,
                                    HIRCodeGen *codeGenerator,
                                    bool resetOffset,
                                    EncapLabel_t input_symbol)
{
	
  SymbolProto *target_proto = (*(et->GetInfo().first.begin())).second;
  // generate and emit the "before" code for the protocol we're reaching
  GenerateBeforeCode(target_proto);
  // generate and emit the "format" code for the protocol we're reaching
  m_NetVMIRGen->LowerHIRCode(&m_GlobalSymbols.GetProtoCode(target_proto),
                             target_proto,
                             "[ET] Parsing format for target protocol",
                             resetOffset);

  struct ETCodegenCB_info_t info;
  info.instance = this;
  info.dfl_fail_label = defaultFailure;
  // retrieve generic info about the substates...
  map<unsigned long, EncapFSA::State*> substates = et->getSubstatesInfo();
  // 	.. and generate their labels
  for(map<unsigned long, EncapFSA::State*>::iterator i = substates.begin();
      i != substates.end();
      ++i)
    info.substates[i->first] = codeGenerator->NewLabel(LBL_CODE, "ET_internal");

  // start the recursive walk
  info.codeGens.push(codeGenerator); 
  et->walk(ETCodegenCB_newlabel, ETCodegenCB_range, ETCodegenCB_punct, ETCodegenCB_jump,ETCodegenCB_special, &info); //walk is sft.hpp
  
  // complete the work by generating jump code from the output gates
  for(map<unsigned long, EncapFSA::State*>::iterator i = substates.begin();
      i != substates.end();
      ++i)
    if(i->second) {
      // emit the label
      map<unsigned long,SymbolLabel *>::iterator l_iter = info.substates.find(i->first);
      nbASSERT(l_iter != info.substates.end(), "I should have a created a label before");
      codeGenerator->LabelStatement(l_iter->second);

      // jump to the outer-level state
      sInfoMap_t::const_iterator infoPtr = stateMappings.find(i->second);
      if (infoPtr == stateMappings.end()) // This gate points to a State that does not lead to an accepting state
        codeGenerator->JumpStatement(defaultFailure);
      else
     {
      	if(!resetOffset)
 	       codeGenerator->JumpStatement(infoPtr->second->getLabelFast()); /* here I use the "fast" label because
                                                                                  * I will emit 'before' and 'format' directly
                                                                                 * inside the ET
                                                                                */
       	else
       		codeGenerator->JumpStatement(infoPtr->second->getLabelComplete());
      }
    }
}

bool NetPFLFrontEnd::GenerateBeforeCode(SymbolProto *sp) {
  nbASSERT(sp, "protocol cannot be NULL inside GenerateBeforeCode");
  
  _nbNetPDLElementProto *element=sp->Info;
  if (sp->FirstExecuteBefore){
    CodeList *beforeCode = m_GlobalSymbols.NewCodeList(true);
    HIRCodeGen beforeCodeGen(m_GlobalSymbols, beforeCode);
    NetPDLParser.ParseBeforeSection(element->FirstExecuteBefore,beforeCodeGen);
    // emit the before code
    m_NetVMIRGen->LowerHIRCode(beforeCode, "emitting before code for proto" + sp->Name);
    m_MIRCodeGen->CommentStatement("DONE emitting before code");

    return true;
  }

  return false;
}


HIROpcodes NetPFLFrontEnd::rangeOpToIROp(RangeOperator_t op){
  switch(op){
  case LESS_THAN: return IR_LTI;
  case LESS_EQUAL_THAN: return IR_LEI;
  case GREAT_THAN: return IR_GTI;
  case GREAT_EQUAL_THAN: return IR_GEI;
  case EQUAL: return IR_EQI;
  case INVALID:
  default:
    nbASSERT(false, "Congratulations, you have found a bug");
    return IR_EQI; // I guess that this is an error code //cannot be here! return value is only for the compiler
  }
}


void NetPFLFrontEnd::ETCodegenCB_newlabel(unsigned long id, string proto_field_name, void *ptr){
  struct ETCodegenCB_info_t *info = (struct ETCodegenCB_info_t*) ptr;

  // safety check
  nbASSERT(info->codeGens.size() == 1, "There should be only one codegen on the stack");

  // save the field symbol that will be used until the next invocation of this method
  size_t dot_pos = proto_field_name.find('.');
  nbASSERT(dot_pos!=string::npos, "I should find a dot in the proto-field name!");
  string proto_name = proto_field_name.substr(0, dot_pos);
  string field_name = proto_field_name.substr(dot_pos+1);
  if(field_name == string("HeaderCounter") or field_name == string("LevelCounter"))
  {
  	//et on a variable related to the symbols - we want to check: variable==n or variable>n
	SymbolProto *proto = info->instance->m_GlobalSymbols.LookUpProto(proto_name);
	EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, proto);//label representing all the symbols leading to proto
	SymbolTemp *st = (field_name == string("HeaderCounter"))? info->instance->m_fsa->GetVariable1(label) : info->instance->m_fsa->GetVariable2(label); //get the variable to test
	nbASSERT(st!=NULL,"This symbol cannot be NULL. There is a bug!");
	nbASSERT(st->SymKind==SYM_TEMP,"There is a bug! This symbol should be SYM_TEMP");
  	info->cur_sym = st;
  }
  else
  	//et on a protocol field
  	info->cur_sym = info->instance->m_GlobalSymbols.LookUpProtoFieldByName(proto_name,field_name);

  // Duplicate the codegen at the top of the stack.
  // It will be used for the current substate.
  HIRCodeGen *codegen = static_cast<HIRCodeGen*>(info->codeGens.top());
  info->codeGens.push(codegen);

  // emit the label
  map<unsigned long,SymbolLabel *>::const_iterator m = info->substates.find(id);
  nbASSERT(m != info->substates.end(), "I should have a created a label before");
  codegen->LabelStatement(m->second);
}


void NetPFLFrontEnd::ETCodegenCB_range(RangeOperator_t r, uint32 sep, void *ptr){
  struct ETCodegenCB_info_t *info = (struct ETCodegenCB_info_t*) ptr;

  // use the codegen at the top of the stack, then pop it
  HIRCodeGen *codegen = static_cast<HIRCodeGen*>(info->codeGens.top());
  info->codeGens.pop();
  
  Symbol *symbol = info->cur_sym;
  StmtIf *ifStmt = NULL;
  
  if(symbol->SymKind==SYM_FIELD)
  	//et on a protocol field
  	ifStmt = codegen->IfStatement(codegen->BinOp(rangeOpToIROp(r),codegen->UnOp(IR_CINT,codegen->TermNode(IR_FIELD, static_cast<SymbolField*>(symbol))),codegen->TermNode(IR_ICONST, codegen->ConstIntSymbol(sep))));
  else if (symbol->SymKind==SYM_TEMP)
		//et on a variable
	ifStmt = codegen->IfStatement(codegen->BinOp(IR_GEI,codegen->UnOp(IR_IVAR,symbol),codegen->TermNode(IR_ICONST,codegen->ConstIntSymbol(2))));
  else
	nbASSERT(false,"There is a bug!");

  // generate the true and false branches, push them into the stack (watch for the order!)
  HIRCodeGen *branchCodeGen = new HIRCodeGen(info->instance->m_GlobalSymbols, ifStmt->TrueBlock->Code);
  HIRCodeGen *falseCodeGen = new HIRCodeGen(info->instance->m_GlobalSymbols, ifStmt->FalseBlock->Code);

  info->codeGens.push(falseCodeGen);
  info->codeGens.push(branchCodeGen);
}

void NetPFLFrontEnd::ETCodegenCB_punct(RangeOperator_t r, const map<uint32,unsigned long> &mappings, void *ptr)
{
  struct ETCodegenCB_info_t *info = (struct ETCodegenCB_info_t*) ptr;

  // use the codegen at the top of the stack, then pop it
  HIRCodeGen *codegen = static_cast<HIRCodeGen*>(info->codeGens.top());
  info->codeGens.pop();

  HIRStmtSwitch *swStmt = NULL;
	
	Symbol *symbol = info->cur_sym;
	
	if(symbol->SymKind==SYM_FIELD)
		//et on a protocol field
		swStmt = codegen->SwitchStatement(codegen->UnOp(IR_CINT,codegen->TermNode(IR_FIELD, static_cast<SymbolField*>(symbol))));
	else if (symbol->SymKind==SYM_TEMP)
		//et on a variable
		swStmt = codegen->SwitchStatement(codegen->UnOp(IR_IVAR,symbol));
	else
		nbASSERT(false,"There is a bug!");

	swStmt->SwExit = codegen->NewLabel(LBL_CODE, "unused");

  // generate the code for the cases
  for(map<uint32,unsigned long>::const_iterator i = mappings.begin();
      i != mappings.end();
      ++i) {
    // prepare the jump for the current case ...
    HIRNode *value = codegen->TermNode(IR_ICONST, codegen->ConstIntSymbol(i->first));
    StmtCase *caseStmt = codegen->CaseStatement(swStmt, value);
    HIRCodeGen subCode(info->instance->m_GlobalSymbols, caseStmt->Code->Code);

    // ... and handle it
    map<unsigned long,SymbolLabel *>::const_iterator m = info->substates.find(i->second);
    nbASSERT(m != info->substates.end(), "Callback for a substate that I did not know of!");
    subCode.JumpStatement(m->second); 

    swStmt->NumCases++;
  }

  // ensure correctness and avoid runtime failures
  codegen->JumpStatement(/*JUMPW, */info->dfl_fail_label); 
  codegen->CommentStatement("The label and jump just above me should never be reached");

  // handle the default case by creating a new IRCodeGen and pushing it onto the stack
  StmtCase *defaultCaseStmt = codegen->DefaultStatement(swStmt);
  HIRCodeGen *dflCaseCodeGen = new HIRCodeGen(info->instance->m_GlobalSymbols, defaultCaseStmt->Code->Code);
  info->codeGens.push(dflCaseCodeGen);
}


void NetPFLFrontEnd::ETCodegenCB_jump(unsigned long id, void *ptr){
  struct ETCodegenCB_info_t *info = (struct ETCodegenCB_info_t*) ptr;

  // use the codegen at the top of the stack, then pop it
  HIRCodeGen *codegen = static_cast<HIRCodeGen*>(info->codeGens.top());
  info->codeGens.pop();

  // jump!
  map<unsigned long,SymbolLabel *>::const_iterator m = info->substates.find(id);
  nbASSERT(m != info->substates.end(), "Callback for a substate that I did not know of!");
  codegen->JumpStatement( m->second); 
}


FieldsList_t NetPFLFrontEnd::GetExField(void)
{
  return m_FieldsList;
}


SymbolField* NetPFLFrontEnd::GetFieldbyId(const string protoName, int index)
{
	return m_GlobalSymbols.LookUpProtoFieldById(protoName,(uint32)index);
}

void NetPFLFrontEnd::ETCodegenCB_special(RangeOperator_t r, std::string val, void *ptr){
	  struct ETCodegenCB_info_t *info = (struct ETCodegenCB_info_t*) ptr;

 	// use the codegen at the top of the stack
 	HIRCodeGen *codegen = static_cast<HIRCodeGen*>(info->codeGens.top());
 	info->codeGens.pop();
 	
 	Symbol *symbol = info->cur_sym;
  	nbASSERT(symbol->SymKind==SYM_FIELD,"There is an error! A special is supported only on et related to fields");

 	SymbolField *sf = static_cast<SymbolField*>(symbol);
 	
 	PDLFieldType fieldType = sf->FieldType;
 	 	
 	if((r==MATCH)||(r==CONTAINS)){//we have to match a regular expression
 	
	 	SymbolStrConst *patternSym = (SymbolStrConst *)codegen->ConstStrSymbol(val);
	 	int id=(r==MATCH)? info->instance->m_GlobalSymbols.GetRegExEntriesCount() : info->instance->m_GlobalSymbols.GetStringMatchingEntriesCount();
 		SymbolTemp *size(0);
 		SymbolTemp *offset(0);
 		bool sensitive;
 		
 		switch(fieldType){
			case PDL_FIELD_VARLEN:
			{
	 			SymbolFieldVarLen *varlenFieldSym = static_cast<SymbolFieldVarLen*>(sf);
 				size  = varlenFieldSym->LenTemp;
 				offset= varlenFieldSym->IndexTemp;	 			
 				sensitive = varlenFieldSym->Sensitive;
			}
			break;
			case PDL_FIELD_TOKEND:
			{
 				SymbolFieldTokEnd *tokEndFieldSym = static_cast<SymbolFieldTokEnd*>(sf);
 				size  = tokEndFieldSym->LenTemp;
 				offset= tokEndFieldSym->IndexTemp;	
 				sensitive = tokEndFieldSym->Sensitive; 			
 			}
			break;
            case PDL_FIELD_LINE:
 			{
				SymbolFieldLine *lineFieldSym = static_cast<SymbolFieldLine*>(sf);
	 			size  = lineFieldSym->LenTemp;
 				offset= lineFieldSym->IndexTemp;	
 				sensitive = lineFieldSym->Sensitive; 			
	 		}	
			break;
			case PDL_FIELD_PATTERN:
			{
	 			SymbolFieldPattern *patternFieldSym = static_cast<SymbolFieldPattern*>(sf);
	 			size  = patternFieldSym->LenTemp;
	 			offset= patternFieldSym->IndexTemp;	
	 			sensitive = patternFieldSym->Sensitive; 			
	 		}		
			break;
			case PDL_FIELD_EATALL:
			{
	 			SymbolFieldEatAll *eatAllFieldSym = static_cast<SymbolFieldEatAll*>(sf);
 				size  = eatAllFieldSym->LenTemp;
 				offset= eatAllFieldSym->IndexTemp;	
 				sensitive = eatAllFieldSym->Sensitive; 			
 			}	
			break;
	 		case PDL_FIELD_FIXED:
			{		
	 			SymbolFieldFixed *fixedFieldSym = static_cast<SymbolFieldFixed*>(sf);	
 				offset = fixedFieldSym->IndexTemp;
  				size  = fixedFieldSym->LenTemp;
  				sensitive = fixedFieldSym->Sensitive;
			}
			break;
 			default:
				nbASSERT(false,"Only fixed, variable, tokenended, line, pattern and eatall fields are supported with the NetPFL keywords \"matches\" and \"contains\"");
 		}
	 	
	 	SymbolRegExPFL *regExpSym = new SymbolRegExPFL(patternSym,sensitive,id,offset,size); 
	 		
	 	HIRNode *exp;
	 	if(r==MATCH){
		 	exp = codegen->TermNode(IR_REGEXFND, regExpSym);
		 	info->instance->m_GlobalSymbols.StoreRegExEntry(regExpSym);
		 	}
		 else {//we are sure that r==CONTAINS
		 	exp = codegen->TermNode(IR_STRINGMATCHINGFND, regExpSym);
		 	info->instance->m_GlobalSymbols.StoreStringMatchingEntry(regExpSym);
		 	}
	 	
	 	
	 	StmtIf *ifStmt = codegen->IfStatement(exp);

		 // generate the true and false branches, push them into the stack (watch for the order!)
		 HIRCodeGen *branchCodeGen = new HIRCodeGen(info->instance->m_GlobalSymbols, ifStmt->TrueBlock->Code);
		 HIRCodeGen *falseCodeGen = new HIRCodeGen(info->instance->m_GlobalSymbols, ifStmt->FalseBlock->Code);

		 info->codeGens.push(falseCodeGen);
		 info->codeGens.push(branchCodeGen);
		 
	}  
 	else
 		nbASSERT(false,"You cannot be here!");
  
}
