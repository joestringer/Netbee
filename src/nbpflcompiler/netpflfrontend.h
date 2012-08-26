/*****************************************************************************/
/*                                                                           */
/* Copyright notice: please read file license.txt in the NetBee root folder. */
/*                                                                           */
/*****************************************************************************/



#pragma once


#include "defs.h"
#include "symbols.h"
#include "errors.h"
#include <nbprotodb.h>
#include "ircodegen.h"
#include "globalsymbols.h"
#include "digraph.h"
#include "compunit.h"
#include "pflexpression.h"
#include "pdlparser.h"
#include "irlowering.h"
#include "pflcfg.h"
#include "encapfsa.h"
#include <map>
#include "sft/librange/range.hpp"
#include <stdlib.h>

using namespace std;

#define INFO_FIELDS_SIZE 4 //!< number of bytes for each fields in the info-partition
#define MAX_PROTO_INSTANCES 5 //!< max number of occurrences of a single protocol handled in the info-partition
#define MAX_FIELD_INSTANCES 5 //!< max number of occurrences of the same field handled in the info-partition


/*!
\brief This class represents the NPFL NetPFLFrontEnd
*/

typedef map<EncapGraph::GraphNode*, EncapGraph::GraphNode*> NodeMap_t;


// forward declarations
class StateCodeInfo;

// typedefs
typedef map<EncapFSA::State*, StateCodeInfo*> sInfoMap_t;
typedef pair<EncapFSA::State*, StateCodeInfo*> sInfoPair_t;



class NetPFLFrontEnd//: public EncapGraph::IGraphVisitor
{
private:

	struct FilterCodeGenInfo
		{
			EncapFSA				&fsa;
			SymbolLabel				*FilterFalseLbl;
			SymbolLabel				*FilterTrueLbl;
			//SymbolProto				*Protocol;


			FilterCodeGenInfo(EncapFSA &fsa, SymbolLabel *filterTrue, SymbolLabel *filterFalse)
				:fsa(fsa), FilterFalseLbl(filterFalse),
				FilterTrueLbl(filterTrue)/*, Protocol(proto)*/ {}
		};







	_nbNetPDLDatabase   *m_ProtoDB;			//!< Reference to the NetPDL protocol database
	ErrorRecorder       m_ErrorRecorder;	//!< The Error Recorder (for tracking compilation errors/warnings)
	GlobalInfo          m_GlobalInfo;
	GlobalSymbols       m_GlobalSymbols;
	CompilationUnit	    *m_CompUnit;
	MIRCodeGen           *m_MIRCodeGen;
	IRLowering          *m_NetVMIRGen;
	bool                m_StartProtoGenerated;
	//FilterCodeGenInfo *m_ProtoFilterInfo;
	//PDLParser         *m_PDLParser;
	PDLParser           NetPDLParser;
	ostream             *HIR_FilterCode;
	ostream	            *NetVMIR_FilterCode;
	ostream	            *PFL_Tree;
	string              NetIL_FilterCode;
	FieldsList_t        m_FieldsList;   //!< Fields that will be extracted
	EncapFSA			*m_fsa;			//!< fsa related to the entire fitlering expression

#ifdef OPTIMIZE_SIZED_LOOPS
	FieldsList_t        m_ReferredFieldsInFilter;
	FieldsList_t        m_ReferredFieldsInCode;
	uint32              m_DefinedReferredFieldsInFilter;
	bool                m_ParsingFilter;
	bool                m_ContinueParsingCode;
	SymbolProto         *m_Protocol;
#endif

        std::pair<string, JumpInfo*> initMapItem(string name,
                                                 PFLExpression *expr,
                                                 SymbolLabel *trueLabel,
                                                 SymbolLabel *falseLabel);

	void GenProtoCode(EncapFSA::State *stateFrom, FilterCodeGenInfo &protoFilterInfo, const sInfoMap_t statesInfo, bool fieldExtraction);
	
        // this data structure and method are needed inside GenProtoCode()
        struct EarlyCodeEmission_t
        {
          SymbolLabel *start_lbl;
          SymbolProto *protocol;
          SymbolLabel *where_to_jump_after;
        };

        SymbolLabel* setupEarlyCode(std::map<string,EarlyCodeEmission_t>* m,
                                            SymbolProto *p,
                                            SymbolLabel *jump_after/*, SymbolLabel *jump_after_after*/, bool needFormat);

        /* Generate code for the provided ExtendedTransition, using
         * the (provided as well) IRCodeGen. Use the map to resolve
         * label references towards external states (those that are
         * part of the general FSA). The defaultFailure label should
         * be used whenever a generic failure jump must be handled.
         */
        void GenerateETCode(EncapFSA::ExtendedTransition *et, const sInfoMap_t stateMappings, SymbolLabel *defaultFailure, HIRCodeGen *codeGenerator, bool fieldExtraction,EncapLabel_t input_symbol);
        
        /*Generate the code for the provided symbol */
        void GenerateSymbolsCode(EncapLabel_t symbol, SymbolLabel* destination);

        // the following group of types and methods is used by the previous method
        struct ETCodegenCB_info_t {
          NetPFLFrontEnd *instance;
          Symbol/*Field*/ *cur_sym;				//this is a general symbol, because an exended transition can be on a field or a variable
          map<unsigned long,SymbolLabel *> substates;
          stack<IRCodeGen *> codeGens;
          SymbolLabel *dfl_fail_label; // label to jump to for default failure
        };
        static HIROpcodes rangeOpToIROp(RangeOperator_t op);
        static void ETCodegenCB_newlabel(unsigned long id, string proto_field_name, void *ptr);
        static void ETCodegenCB_range(RangeOperator_t r, uint32 sep, void *ptr);
        static void ETCodegenCB_punct(RangeOperator_t r, const map<uint32,unsigned long> &mappings, void *ptr);
        static void ETCodegenCB_jump(unsigned long id, void *ptr);
	static void ETCodegenCB_special(RangeOperator_t r, std::string val, void *ptr);

        /* Generate and emit the before code (if applicable) for the provided protocol
         * Returns true if code was actually generated, false otherwise
         */
        bool GenerateBeforeCode(SymbolProto *proto);

        /* Extract an IR expression from an arbitrary complex
         * PFLExpression, using the provided code generator if needed.
         *
         * Return NULL if this is impossible (predicates on different
         * protocols).
         */
        //HIRNode* extractComplexIRExpr(PFLExpression *expr, HIRCodeGen *codeGen); [icerrato]UNUSED

	int GenCode(PFLStatement *filterExpr);


        // [dark] UNUSED
	// void CloneHIR(CodeList &code, IRCodeGen &codegen);

	void GenInitCode(void);

	void GenRegexEntries(void);
	
	void GenStringMatchingEntries(void);

	void DumpPFLTreeDotFormat(PFLExpression *expr, ostream &stream);

	void GenFSACode(EncapFSA *fsa, SymbolLabel *trueLabel, SymbolLabel *falseLabel, bool fieldExtraction);

	EncapFSA *VisitFilterExpression(PFLExpression *expr, bool fieldExtraction, NodeList_t toExtract);

	EncapFSA *VisitFilterBinExpr(PFLBinaryExpression *expr, bool fieldExtraction, NodeList_t toExtract);

	EncapFSA *VisitFilterUnExpr(PFLUnaryExpression *expr, bool fieldExtraction, NodeList_t toExtract);

	EncapFSA *GenFieldExtract(NodeList_t &protocols, SymbolLabel *trueLabel, SymbolLabel *falseLabel, EncapFSA *fsa);

	EncapFSA *VisitFilterTermExpr(PFLTermExpression *expr);
	
	EncapFSA *VisitNullExpr();
	
	void ReorderStates(list<EncapFSA::State*> *stateList);
	
	void GenHeaderIndexingCode(EncapFSA *fsa);
	
	void GenHeaderTunneledCode(EncapFSA *fsa);
	
	EncapFSA *VisitFilterRegExpr(PFLRegExpExpression *expr, bool fieldExtraction, NodeList_t toExtract);

        void ParseExtractField(FieldsList_t *fldList);

        void VisitAction(PFLAction *action);

	void SetupCompilerConsts(CompilerConsts &NodecompilerConsts, uint16 linkLayer);

	PFLStatement *ParseFilter(string filter);

#ifdef OPTIMIZE_SIZED_LOOPS
	void GetReferredFields(CodeList *code)
	{
		m_ParsingFilter= false;
		m_DefinedReferredFieldsInFilter=0;

		VisitCode(code);
	}

	void GetReferredFieldsExtractField(SymbolProto *proto)
	{
		CodeList *code = &m_GlobalSymbols.GetFieldExtractCode(proto);
		m_ContinueParsingCode= true;
	    VisitCode(code);
		m_ReferredFieldsInFilter.unique();
	}

	void VisitCode(CodeList *code)
	{
		StmtBase *next = code->Front();

		while(next && m_ContinueParsingCode)
		{
			VisitStatement(next);
			next = next->Next;
		}
	}

	void VisitStatement(StmtBase *stmt)
	{
		switch(stmt->Kind)
		{
		case STMT_GEN:
			VisitGen((StmtGen*)stmt);
			break;
		case STMT_JUMP:
			VisitJump((StmtJump*)stmt);
			break;
		case STMT_SWITCH:
			VisitSwitch((StmtSwitch*)stmt);
			break;
		case STMT_IF:
			VisitIf((StmtIf*)stmt);
			break;
		case STMT_LOOP:
			VisitLoop((StmtLoop*)stmt);
			break;
		case STMT_WHILE:
			VisitWhileDo((StmtWhile*)stmt);
			break;
		case STMT_DO_WHILE:
			VisitDoWhile((StmtWhile*)stmt);
			break;
		case STMT_FINFOST:
			VisitFldInfo((StmtFieldInfo*)stmt);
			break;
		default:
			break;
		}
	}

	void VisitLoop(StmtLoop *stmt)
	{
		VisitTree(static_cast<HIRNode*>(stmt->Forest));
		VisitTree(static_cast<HIRNode*>(stmt->InitVal));

		//loop body
		VisitCode(stmt->Code->Code);

		//<inc_statement>
		VisitStatement(stmt->IncStmt);

		VisitBoolExpression(static_cast<HIRNode*>(stmt->TermCond));
	}

	void VisitWhileDo(StmtWhile *stmt)
	{
		VisitCode(stmt->Code->Code);


		VisitBoolExpression(static_cast<HIRNode*>(stmt->Forest));
	}

	void VisitDoWhile(StmtWhile *stmt)
	{
		VisitCode(stmt->Code->Code);

		VisitBoolExpression(static_cast<HIRNode*>(stmt->Forest));
	}

	void VisitIf(StmtIf *stmt)
	{
		HIRNode *expr = static_cast<HIRNode*>(stmt->Forest);

		VisitBoolExpression(expr);

		if (stmt->TrueBlock->Code->Empty()==false)
		{
			VisitCode(stmt->TrueBlock->Code);

			if(stmt->FalseBlock->Code->Empty()==false)
			{
				//code for the false branch
				VisitCode(stmt->FalseBlock->Code);
			}
		}
	}

	void VisitJump(StmtJump *stmt)
	{
		VisitBoolExpression(static_cast<HIRNode*>(stmt->Forest));
	}

	void VisitSwitch(StmtSwitch *stmt)
	{
		VisitTree(static_cast<HIRNode*>(stmt->Forest));

		VisitCases(stmt->Cases);
		if (stmt->Default != NULL)
		{
			VisitCase(stmt->Default, true);
		}
	}

	void VisitCases(CodeList *cases)
	{
		StmtBase *caseSt = cases->Front();
		while(caseSt)
		{
			VisitCase((StmtCase*)caseSt, false);
			caseSt = caseSt->Next;
		}
	}

	void VisitCase(StmtCase *caseSt, bool IsDefault)
	{
		//if the code for the current case is empty we drop it
		//! \todo manage the number of cases!
		if (caseSt->Code->Code->Empty())
			return;

		if (!IsDefault)
		{
			VisitTree(static_cast<HIRNode*>(caseSt->Forest));
		}

		VisitCode(caseSt->Code->Code);
	}


	void VisitGen(StmtGen *stmt)
	{
		nbASSERT(stmt != NULL, "stmt cannot be NULL");
		nbASSERT(stmt->Forest != NULL, "Forest cannot be NULL");
		HIRNode *tree = static_cast<HIRNode*>(stmt->Forest);

		switch (tree->Op)
		{
		case IR_DEFFLD:
			VisitFieldDef(tree);
			break;

		case IR_ASGNI:
			VisitAssignInt(tree);
			break;

		case IR_ASGNS:
			VisitAssignStr(tree);
			break;

			//case IR_LKADD:
			//	TranslateLookupAdd(tree);
			//	break;

			//case IR_LKDEL:
			//	TranslateLookupDelete(tree);
			//	break;

			//case IR_LKHIT:
			//case IR_LKSEL:
			//	{
			//		SymbolLookupTableEntry *entry = (SymbolLookupTableEntry *)tree->Sym;
			//		nbASSERT(tree->GetLeftChild() != NULL, "Lookup select instruction should specify a keys list");
			//		SymbolLookupTableKeysList *keys = (SymbolLookupTableKeysList *)tree->GetLeftChild()->Sym;
			//		TranslateLookupSelect(entry, keys);
			//	}break;

			//case IR_LKUPDS:
			//case IR_LKUPDI:
			//	TranslateLookupUpdate(tree);
			//	break;

			//case IR_LKINIT:
			//	TranslateLookupInit(tree);
			//	break;

			//case IR_LKINITTAB:
			//	TranslateLookupInitTable(tree);
			//	break;
		}
	}

	void VisitAssignInt(HIRNode *node)
	{
		node->GetLeftChild();
		// Node *leftChild = node->GetLeftChild();
		HIRNode *rightChild = node->GetRightChild();

		VisitTree(rightChild);
	}

	void VisitAssignStr(HIRNode *node)
	{
		HIRNode *leftChild = node->GetLeftChild();
		node->GetRightChild();
		// Node *rightChild = node->GetRightChild();
		SymbolVariable *varSym = (SymbolVariable*)leftChild->Sym;

		switch (varSym->VarType)
		{
		case PDL_RT_VAR_REFERENCE:
			{
				// SymbolVarBufRef *ref=(SymbolVarBufRef *)varSym;
				SymbolField *referee=(SymbolField *)node->Sym;

				switch (referee->FieldType)
				{
				case PDL_FIELD_VARLEN:
					{	
					// SymbolFieldVarLen *varLenField=(SymbolFieldVarLen *)referee;
					break;
					}
				case PDL_FIELD_TOKEND:
					{
					// SymbolFieldTokEnd *tokEndField=(SymbolFieldTokEnd *)referee;
					break;
					}
				case PDL_FIELD_TOKWRAP:
					{
					// SymbolFieldTokWrap *tokWrapField=(SymbolFieldTokWrap *)referee;
					break;
					}
				case PDL_FIELD_LINE:
					{
					// SymbolFieldLine *lineField=(SymbolFieldLine *)referee;
					break;
					}
				case PDL_FIELD_PATTERN:
					{
					// SymbolFieldPattern *patternField=(SymbolFieldPattern *)referee;
					break;
					}
				case PDL_FIELD_EATALL:
					{
					// SymbolFieldEatAll *eatAllField=(SymbolFieldEatAll *)referee;
					break;
					}
				default:
					break;
					}
			}
		default:
			break;
		}
	}


	void VisitFieldDef(HIRNode *node)
	{
		HIRNode *leftChild = node->GetLeftChild();
		SymbolField *fieldSym = (SymbolField*)leftChild->Sym;

		SymbolField *sym=this->m_GlobalSymbols.LookUpProtoField(this->m_Protocol, fieldSym);

		switch(fieldSym->FieldType)
		{
			case PDL_FIELD_FIXED:
				break;
			case PDL_FIELD_VARLEN:
			{
				SymbolFieldVarLen *varlenFieldSym = (SymbolFieldVarLen*)sym;

				VisitTree(static_cast<HIRNode*>(varlenFieldSym->LenExpr));
			}break;

		case PDL_FIELD_TOKEND:
			{
				SymbolFieldTokEnd *tokendFieldSym = (SymbolFieldTokEnd*)sym;
				if(tokendFieldSym->EndOff!=NULL)
					VisitTree(static_cast<HIRNode*>(tokendFieldSym->EndOff));
				if(tokendFieldSym->EndDiscard!=NULL)
					VisitTree(static_cast<HIRNode*>(tokendFieldSym->EndDiscard));
			}break;


		case PDL_FIELD_TOKWRAP:
			{
				SymbolFieldTokWrap *tokwrapFieldSym = (SymbolFieldTokWrap*)sym;

				if(tokwrapFieldSym->BeginOff!=NULL)
					VisitTree(static_cast<HIRNode*>(tokwrapFieldSym->BeginOff));
				if(tokwrapFieldSym->EndOff!=NULL)
					VisitTree(static_cast<HIRNode*>(tokwrapFieldSym->EndOff));
				if(tokwrapFieldSym->EndDiscard!=NULL)
					VisitTree(static_cast<HIRNode*>(tokwrapFieldSym->EndDiscard));

			}break;

		case PDL_FIELD_LINE:
		case PDL_FIELD_PATTERN:
		case PDL_FIELD_EATALL:
			break;
		default:
			break;
		}

		bool usedField=false;
		for (FieldsList_t::iterator i=m_ReferredFieldsInFilter.begin(); i!=m_ReferredFieldsInFilter.end() && usedField==false; i++)
		{
			if ( (*i)==sym)
			{
				usedField=true;
			}
		}

		if (usedField)
		{
			m_DefinedReferredFieldsInFilter++;

			if (m_DefinedReferredFieldsInFilter==m_ReferredFieldsInFilter.size())
				m_ContinueParsingCode=false;
		}

	}

	void VisitFldInfo(StmtFieldInfo* stmt)
	{
		if(stmt->Field->FieldType==PDL_FIELD_ALLFIELDS)
		{
			SymbolProto * proto=stmt->Field->Protocol;
			SymbolField **list=m_GlobalSymbols.GetFieldList(proto);
			for(int i =0;i < (int)m_GlobalSymbols.GetNumField(proto);i++)
			{
			   m_ReferredFieldsInFilter.push_front(list[i]);
			}

		}
		else
			m_ReferredFieldsInFilter.push_front(stmt->Field);
	}

	void VisitBoolExpression(HIRNode *expr)
	{
		switch(expr->Op)
		{
		case IR_NOTB:
			VisitBoolExpression(expr->GetLeftChild());
			break;
		case IR_ANDB:
		case IR_ORB:
			VisitBoolExpression(expr->GetLeftChild());
			VisitBoolExpression(expr->GetRightChild());
			break;
		case IR_EQI:
		case IR_GEI:
		case IR_GTI:
		case IR_LEI:
		case IR_LTI:
		case IR_NEI:
		case IR_EQS:
		case IR_NES:
		//case IR_GTS: [icerrato] these operation are not allowed in the NetPDL language
		//case IR_LTS:
			VisitTree(expr->GetLeftChild());
			VisitTree(expr->GetRightChild());
			break;
			//case IR_IVAR:
			//	{
			//		SymbolVariable *sym = (SymbolVariable *)expr->Sym;
			//	};break;
		case IR_CINT:
			VisitCInt(expr->GetLeftChild());
			break;
		}
	}

	void VisitTree(HIRNode *node)
	{
		switch(node->Op)
		{
			//case IR_IVAR:
			//	TranslateIntVarToInt(node);
			//	break;
		case IR_CINT:
			VisitCInt(node);
			break;
		case IR_FIELD:
			SymbolField *fieldSym = (SymbolField*)node->Sym;
			if (m_ParsingFilter)
			{
				m_ReferredFieldsInFilter.push_front(fieldSym);
			}
			else
			{
				m_ReferredFieldsInCode.push_front(fieldSym);
			}
			break;
			//case IR_SVAR:
			//	{
			//		SymbolVarBufRef *ref = (SymbolVarBufRef*)node->Sym;
			//	}break;
		}
	}

	void VisitCInt(HIRNode *node)
	{
		node->GetLeftChild();
		// Node *child = node->GetLeftChild();

		switch (node->Op)
		{
			//case IR_IVAR:
			//	return TranslateIntVarToInt(child);
			//	break;
			//case IR_SVAR:
			//	return TranslateStrVarToInt(child, offsNode, size);
			//	break;
		case IR_FIELD:
			SymbolField *fieldSym = (SymbolField*)node->Sym;
			if (m_ParsingFilter)
			{
				m_ReferredFieldsInFilter.push_front(fieldSym);
			}
			else
			{
				m_ReferredFieldsInCode.push_front(fieldSym);
			}
			break;
		}
	}
#endif

public:
	/*!
	\brief	Object constructor

	\param	protoDB		reference to a NetPDL protocol database

	\param	errRecorder reference to an ErrorRecorder for tracking errors and warnings

	\return nothing
	*/


	NetPFLFrontEnd(_nbNetPDLDatabase &protoDB, nbNetPDLLinkLayer_t LinkLayer,
		const unsigned int DebugLevel=0, const char *dumpHIRCodeFilename= NULL, const char *dumpMIRNoOptGraphFilename=NULL, const char *dumpProtoGraphFilename=NULL);

	/*!
	\brief	Object destructor
	*/
	~NetPFLFrontEnd();

    bool CheckFilter(string filter);
    
    int CreateAutomatonFromFilter(string filter);

	int CompileFilter(string filter, bool optimizationCycles = true);//it can return nbSUCCESS, nbFAILURE or nbWARNING

	ErrorRecorder &GetErrRecorder(void)

	{
		return m_ErrorRecorder;
	}

	string &GetNetILFilter(void)
	{
		return NetIL_FilterCode;
	}
	
	void PrintFinalAutomaton(const char *dotfilename)
	{
	  if(m_fsa==NULL)
	  	return;		
	  ofstream outfile(dotfilename);
	  sftDotWriter<EncapFSA> fsaWriter(outfile);
	  fsaWriter.DumpFSA(m_fsa);			
	}

	void DumpCFG(ostream &stream, bool graphOnly, bool netIL);

	void DumpFilter(ostream &stream, bool netIL);

	FieldsList_t GetExField(void);

	SymbolField* GetFieldbyId(const string protoName, int index);


};


/*
 * Class used to store various information about the FSA states
 * of which we have to generate code.
 */
class StateCodeInfo
{
 private:
  EncapFSA::State *s_ptr;
  SymbolLabel *label_complete; // label emitted at the beginning of the protocol
  SymbolLabel *label_fast; /* label emitted after the 'before' and 'format' sections,*/
                          /*  * but before 'encapsulation' and ExtendedTransition handling
                            */
  //SymbolLabel *label_light;
  string name; // use it when you want to refer to this state as a string

 public:
  // constructor
 StateCodeInfo(EncapFSA::State *state_pointer)
   :s_ptr(state_pointer)
  {
    stringstream ss;
    ss << "state_" << state_pointer->GetID();
    name = ss.str();
  }

 void setLabels(SymbolLabel *label_complete, SymbolLabel *label_fast/*, SymbolLabel *label_light*/){
   this->label_complete = label_complete;
   this->label_fast = label_fast;
 //  this->label_light = label_light;

 }
 SymbolLabel* getLabelComplete(){ return label_complete; }
 SymbolLabel* getLabelFast(){ return label_fast; }
// SymbolLabel* getLabelLight(){ return label_light; }
 string toString(){ return name; }
};
