/*****************************************************************************/
/*                                                                           */
/* Copyright notice: please read file license.txt in the NetBee root folder. */
/*                                                                           */
/*****************************************************************************/





#include "encapfsa.h"
#include "DFAset.h"
#include "assignprotocollayer.h"
#include <iomanip> // for EncapFSA::prettyDotPrint()
#include <fstream> // for EncapFSA::prettyDotPrint()


#define TRANSFER_STATE_PROPERTIES(newfsa_ptr,newst_iter,oldst){         \
    if((oldst).isAccepting())                                           \
      newfsa_ptr->setAccepting(newst_iter);                             \
    else                                                                \
      newfsa_ptr->resetAccepting(newst_iter);                           \
    if((oldst).isFinal())                                               \
      newfsa_ptr->setFinal(newst_iter);                                 \
    else                                                                \
      newfsa_ptr->resetFinal(newst_iter);                               \
  }

EncapGraph *EncapFSA::m_ProtocolGraph = NULL;

/***************************** Boolean operations *****************************/

void EncapFSA::migrateTransitionsAndStates(EncapFSA *to, EncapFSA *from, StateIterator init){
  //state mapping: first term is inputFSA stateiterator, second term is otputFSA stateiterator
  std::map<EncapFSA::State*, EncapFSA::State*> stateMap;
  // ExtendedTransition mapping
  std::map<EncapFSA::ExtendedTransition*, EncapFSA::ExtendedTransition*> et_map;

  EncapFSA::StateIterator init1 = from->GetInitialState();
  EncapFSA::StateIterator s = to->AddState((*init1).GetInfo());
  
  if((*init1).isAccepting())
    to->setAccepting(s);
  else 
    to->resetAccepting(s);
  
  stateMap.insert( std::pair<EncapFSA::State*, EncapFSA::State*>(&(*init1),&(*s)) );
  to->AddEpsilon(init, s, NULL);
  
  for(EncapFSA::TransIterator t = from->FirstTrans(); t != from->LastTrans(); t++) //iterates on each transition of the from fsa
    {
      EncapFSA::State *fromState = &(*(*t).FromState());
      EncapFSA::State *toState = &(*(*t).ToState());

      std::map<EncapFSA::State*, EncapFSA::State*>::iterator i = stateMap.find(fromState);
      if(i == stateMap.end())
        {
          EncapFSA::StateIterator st = to->AddState(fromState->GetInfo());
          TRANSFER_STATE_PROPERTIES(to,st,*fromState);
          stateMap.insert(std::pair<EncapFSA::State*,EncapFSA::State*>(fromState,&(*st)) );
          fromState = &(*st);
        }
      else
        {
          fromState = i->second;
        }
      std::map<EncapFSA::State *, EncapFSA::State *>::iterator j = stateMap.find(toState);
      if(j == stateMap.end())
        {
          EncapFSA::StateIterator st2 = to->AddState(toState->GetInfo());
          TRANSFER_STATE_PROPERTIES(to,st2,*toState);
          stateMap.insert( std::pair<EncapFSA::State*,EncapFSA::State*>(toState,&(*st2)) );
          toState = &(*st2);
        }
      else
        {
          toState = j->second;
        }
      EncapFSA::StateIterator iterFrom = to->GetStateIterator(fromState);
      EncapFSA::StateIterator iterTo = to->GetStateIterator(toState);
      TransIterator new_t = to->AddTrans(iterFrom, iterTo, *t);

      // fix the references to the ExtendedTransitions
      ExtendedTransition *et_ref = (*t).getIncludingET();
      if(et_ref){
        // PRINT_DOT(to, "DBG nel loop di migrate", "migrate_loop");
        std::map<EncapFSA::ExtendedTransition*, EncapFSA::ExtendedTransition*>::iterator e = et_map.find(et_ref);
        if (e == et_map.end()){
              
          // duplicate the extended transition
          ExtendedTransition *new_et = new ExtendedTransition(et_ref);
          
          /*if(!new_et->startingNode->advancedPredicates.empty())
         	printf("The new ExtendedTransition has an AdvancedPredicate\n");*/

          // handle ET gates
          std::list<StateIterator> old_outgates = new_et->ToStates();
          State *old_ingate = new_et->FromState();
          std::map<State*, State*> new_outgates;
          State* new_ingate;

          // input gate
          std::map<EncapFSA::State*, EncapFSA::State*>::iterator in_gate_ref = stateMap.find(old_ingate);
          if(in_gate_ref == stateMap.end()) {
            // was not seen yet, handle it
            EncapFSA::StateIterator st = to->AddState((*old_ingate).GetInfo());
            TRANSFER_STATE_PROPERTIES(to, st, *old_ingate);
            stateMap.insert(std::pair<EncapFSA::State*,EncapFSA::State*>(old_ingate, &(*st)));
            new_ingate = &(*st);
          }
          else {
            new_ingate = in_gate_ref->second;
          }

          // output gates
          for (std::list<StateIterator>::iterator old_outgate_ref = old_outgates.begin();
               old_outgate_ref != old_outgates.end();
               old_outgate_ref++){
            std::map<EncapFSA::State*, EncapFSA::State*>::iterator out_gate_ref = stateMap.find(&(**old_outgate_ref));
            if(out_gate_ref == stateMap.end()) {
              // was not seen yet, handle it
              EncapFSA::StateIterator st = to->AddState((**old_outgate_ref).GetInfo());
              TRANSFER_STATE_PROPERTIES(to, st, **old_outgate_ref);
              stateMap.insert(std::pair<EncapFSA::State*,EncapFSA::State*>(&(**old_outgate_ref), &(*st)));
              new_outgates.insert(std::pair<State*, State*>(&(**old_outgate_ref), &(*st)));
            }
            else {
              new_outgates.insert(std::pair<State*, State*>(&(**old_outgate_ref), out_gate_ref->second));
            }
          }

          // notify the ET about the changes
          new_et->changeGateRefs(to, new_ingate, new_outgates);

          // save the new ET
          et_map.insert(std::pair<EncapFSA::ExtendedTransition*, EncapFSA::ExtendedTransition*>(et_ref, new_et));
          (*new_t).setIncludingET(new_et);
          new_et->changeEdgeRef(t, new_t);
        }
        else{
          (*new_t).setIncludingET(e->second);
          e->second->changeEdgeRef(t, new_t);
        }
      }
    }
}

EncapFSA* EncapFSA::BooleanAND(EncapFSA *fsa1, EncapFSA *fsa2, bool fieldExtraction, NodeList_t toExtract)
{
	fsa1->BooleanNot(); 
	fsa2->BooleanNot();
	EncapFSA* fsa = EncapFSA::BooleanOR(fsa1,fsa2,fieldExtraction, toExtract);
    EncapFSA* result = EncapFSA::NFAtoDFA(fsa, fieldExtraction, toExtract);
	result->BooleanNot();
    result->setFinalStates(fieldExtraction, toExtract);
	
	return result;
}

EncapFSA* EncapFSA::BooleanOR(EncapFSA *fsa1, EncapFSA *fsa2, bool fieldExtraction, NodeList_t toExtract)
{

  nbASSERT(fsa1!=NULL, "fsa1 cannot be NULL");
  nbASSERT(fsa2!=NULL, "fsa2 cannot be NULL");
  EncapFSA *fsa = new EncapFSA(fsa1->m_Alphabet);
  fsa->MergeCode1(fsa1->m_code1,fsa2->m_code1);
  fsa->MergeCode2(fsa1->m_code2,fsa2->m_code2);
  EncapFSA::StateIterator init = fsa->AddState(NULL);
  fsa->SetInitialState(init);

  PRINT_DOT(fsa, "OR-pre-first-loop", "or_pre_first_loop");
  migrateTransitionsAndStates(fsa, fsa1, init);
  PRINT_DOT(fsa, "OR-pre-second-loop", "or_pre_2nd_loop");
  migrateTransitionsAndStates(fsa, fsa2, init);
  PRINT_DOT(fsa, "OR-post-second-loop", "or_post_2nd_loop");

  // EncapFSA *dfsa = EncapFSA::NFAtoDFA(fsa);
  fsa->setFinalStates(fieldExtraction, toExtract);

  PRINT_DOT(fsa, "OR-after-state-fix", "or_after_statefix");

  fsa = EncapFSA::NFAtoDFA(fsa, fieldExtraction, toExtract);
  
  PRINT_DOT(fsa,"After determinization","after_or_determ");


  return fsa;
}

EncapFSA::StateIterator EncapFSA::GetStateIterator(EncapFSA::State *state)
{
	EncapFSA::StateIterator s = this->FirstState();
	while(s != this->LastState())
	{
		if((*s).GetID() == state->GetID())
			break;
		s++;
	}
	return s;
}

EncapFSA::TransIterator EncapFSA::GetTransIterator(Transition *transition)
{
	EncapFSA::TransIterator t = this->FirstTrans();
	while(t != this->LastTrans())
	{
		EncapFSA::StateIterator frIt = (*t).FromState();
		EncapFSA::StateIterator frIt2 = transition->FromState();
		if(frIt == frIt2)
		{
			EncapFSA::StateIterator toIt = (*t).ToState();
			EncapFSA::StateIterator toIt2 = transition->ToState();
			if(toIt == toIt2)
			{
				Alphabet_t alph = (*t).GetLabels();
				Alphabet_t alph2 = transition->GetLabels();
				if(alph == alph2)
					break;
			}
		}
		t++;
	}

	return t;
}



void E_Closure(DFAset* ds, EncapFSA *fsa)
{
	bool mod = false;
	std::map<uint32,EncapFSA::State*> states = ds->GetStates();

	for(EncapFSA::TransIterator t = fsa->FirstTrans(); t != fsa->LastTrans(); t++) //for each transition of the FSA
	{
          	EncapFSA::State *fromState = &(*(*t).FromState());          
		if((states.find(fromState->GetID()) != states.end()) && (*t).IsEpsilon() && (*t).GetPredicate() == NULL && !(*t).IsVisited())
		{
                  EncapFSA::State *toState = &(*(*t).ToState());
			ds->AddState(toState);
			mod = true;
			(*t).SetVisited();
		}
	}

	if(mod)
		E_Closure(ds,fsa);
}

void UnsetVisited(EncapFSA *fsa)
{
	EncapFSA::TransIterator iter = fsa->FirstTrans();
	while(iter != fsa->LastTrans())
	{
		(*iter).ResetVisited();
		iter++;
	}
	return;
}

void Move(DFAset* ds, EncapFSA *fsa, EncapLabel_t sym, bool complement, DFAset* newds)
{
	std::map<uint32,EncapFSA::State*> states = ds->GetStates();

	for(EncapFSA::TransIterator t = fsa->FirstTrans(); t != fsa->LastTrans(); t++)
	{
          EncapFSA::State *fromState = &(*(*t).FromState());
		if(states.find(fromState->GetID()) != states.end())
		{
			if(!complement)
			{
				EncapFSA::Alphabet_t a = (*t).GetInfo().first;
				if((a.contains(sym) && !(*t).IsComplementSet()) || (!a.contains(sym) && (*t).IsComplementSet()))
				{
                                  EncapFSA::State *toState = &( *(*t).ToState() );
                                  newds->AddState(toState);
				}
			}
			else
			{
				if((*t).IsComplementSet())
				{
                                  EncapFSA::State *toState = &( *(*t).ToState() );
                                  newds->AddState(toState);
				}
			}
		}
	}
}

/* Return true if an ExtendedTransition was encountered.
 * et_out_gates (if this method returned true) maps the set of gates 
 * that the ExtendedTransition reaches after determinization to a token value
 * (of type State*) that can be reused to set the gate pointers to a sane value,
 * after that those sets have been successfully recreated in the determinized DFA,
 * using ExtendedTransition::changeGateRefs()
 *
 * newds takes now the semantic "set of states reachable
 * without transition with predicates". That is, if this method returns true,
 * depending on the value of the predicates, the following sets are reached:
 * [pseudocode]
 * foreach (stateset in et_out_gates.first) Union(newds, stateset)
 *
 * at_least_one_complementset_trans_was_taken (*sigh*) says if newds was reached by
 * at least one transition in the form "* - {symbol}" (useful to invalidate
 * the state information in the state equivalent, after the determinization, to newds)
 */
bool MoveExt(DFAset* ds, EncapFSA *fsa, EncapLabel_t sym, DFAset* newds,
             EncapFSA::ExtendedTransition **ptr_to_et, 
             std::map<DFAset*, EncapFSA::State*> *et_out_gates,
             bool *at_least_one_complementset_trans_was_taken)
{
  //newset, fsa, current symbol, extTra, outGates, bool

  std::map<uint32,EncapFSA::State*> states = ds->GetStates();
  EncapFSA::ExtendedTransition *local_et = NULL;
  std::set<EncapFSA::ExtendedTransition *> known_ETs; /* Stores pointers to all the ETs encountered.
                                                       * Used to avoid handling twice or more the same ET
                                                       */
  for(EncapFSA::TransIterator t = fsa->FirstTrans(); t != fsa->LastTrans(); t++)
    {    
      EncapFSA::State *fromState = &(*(*t).FromState());
      if(states.find(fromState->GetID()) != states.end())
        {
          EncapFSA::Alphabet_t a = (*t).GetInfo().first;
          if((a.contains(sym) && !(*t).IsComplementSet()) || (!a.contains(sym) && (*t).IsComplementSet()))
            {
              if ((*t).IsComplementSet())
                *at_least_one_complementset_trans_was_taken = true;
              EncapFSA::State *toState = &(*(*t).ToState());
              
              if ( (*t).getIncludingET() ) {
                // this transition is handled by an extended transition
                EncapFSA::ExtendedTransition *et = (*t).getIncludingET();
                if(known_ETs.count(et)>0)
                	continue; //done already
                else{
                  known_ETs.insert(et); //handling this ET now
                  }

                if (local_et)
                  // add to the already existing ET
                  (*local_et) += (*et);
                else
                  // ET must be created and initialized
                  local_et = new EncapFSA::ExtendedTransition(et);
                 
              } else {
                newds->AddState(toState); // see newds semantic above
                              
                if (local_et)
                  // add to the already existing ET
                  local_et->unionWithoutPredicate(toState, (*t).GetEdge());
              }
            }
        }
    }

  if(local_et==NULL){
    return false;
  }
  			
  local_et->wipeEdgeRefs(); 
  
  // why this conversion? ask to who designed those headers
  std::map<std::set<EncapFSA::State*>, EncapFSA::State*> det_tokens;
  local_et->determinize(&det_tokens);  
  
  for( std::map<std::set<EncapFSA::State*>, EncapFSA::State*>::iterator ii = det_tokens.begin();
       ii != det_tokens.end();
       ++ii) 
    et_out_gates->insert(make_pair<DFAset*, EncapFSA::State*>(new DFAset(ii->first),ii->second));
    

  *ptr_to_et = local_et;

  PRINT_DOT(fsa, "MoveExt end", "moveext_end");
  return true;
}

/*
 * This function populates the DFAset by adding into it all the
 * _symbols_ that appear on non-epsilon, non-complemented transitions
 * that exits from _states_ currently in the DFAset.
 */
void AddSymToDFAset(DFAset* dset, EncapFSA *fsa /*origin FSA*/)
{
	
	std::map<uint32,EncapFSA::State*> states = dset->GetStates();
	for(EncapFSA::TransIterator t = fsa->FirstTrans(); t != fsa->LastTrans(); t++)
	{

          	EncapFSA::State *fromState = &(*((*t).FromState()));
		if(!(*t).IsEpsilon() && !(*t).IsComplementSet() && (states.find(fromState->GetID())) != states.end())
		{
			EncapFSA::Alphabet_t a = (*t).GetInfo().first;
			if(a.size()>0){
				EncapFSA::Alphabet_t::iterator labelIter = a.begin();
				while(labelIter != a.end())
				{
					EncapLabel_t label = *labelIter;
					dset->AddSymbol(label);
					labelIter++;
				}
			}
		}
	}
}


int ExistsDFAset(std::list<DFAset> dlist, DFAset* dset){
	std::list<DFAset>::iterator iter = dlist.begin();
	while(iter != dlist.end()){
		if((*iter).CompareTo(*dset)==0)
			return (*iter).GetId();
		iter++;
	}
	return -1;
}

/*
 * Handle a single movement from a state to another set.
 * Returns <0 if the target DFAset was unseen, >0 otherwise.
 * The absolute value of the returned value is the ID of dfaset_to in dfa_list
 * (that might be different from the ID in dfaset_to, as it might be duplicated).
 *
 * The last parameter, when false, requests that no state information are added
 * to the newly generated state (if applicable, that is if effectively the target
 * stateset was still unseen). This is useful when no consistent state information
 * could be added to the newly created state.
 * Note that, even if the last parameter is true, this method might
 * not set the stateInfo of the target state if it is an accepting final
 * state: we will fix that later (fixStateProtocols).
 */
int HandleSingleMovement(EncapFSA *fsa_from, EncapFSA *fsa_to, EncapFSA::State *state_from, DFAset *dfaset_to,
                         EncapFSA::Alphabet_t::iterator symbol_iter, std::list<DFAset> *dfa_list,
                         std::map<int, EncapFSA::State*> *stateMap, EncapFSA::ExtendedTransition *et,
                         bool set_internal_state_info = true){
  E_Closure(dfaset_to, fsa_from); // FIXME implementare le ottimizzazioni?
  UnsetVisited(fsa_from);
  std::map<uint32,EncapFSA::State*> newdsStates = dfaset_to->GetStates();
  if(newdsStates.size() < 1)
    return false;

  // add the transition
  int id;
  if((id = ExistsDFAset(*dfa_list,dfaset_to)) > 0)
    {
      EncapFSA::State *st = (stateMap->find(id))->second;
      EncapFSA::StateIterator st_1 = fsa_to->GetStateIterator(state_from);
      EncapFSA::StateIterator st_2 = fsa_to->GetStateIterator(st);
      if(et){
        EncapFSA::TransIterator tr_iter = fsa_to->AddTrans(st_1, st_2, *symbol_iter, false, PTraits<PFLTermExpression*>::One());  // FIXME last param (only a printing issue)
        (*tr_iter).setIncludingET(et);
        et->addEdgeRef(tr_iter);
      }
      else
        fsa_to->AddTrans(st_1, st_2, *symbol_iter, false, NULL);
    }
  else
    {
      AddSymToDFAset(dfaset_to, fsa_from);
      if(set_internal_state_info && !dfaset_to->isAcceptingFinal())
        dfaset_to->AutoSetInfo();
      SymbolProto* info = dfaset_to->GetInfo();
      EncapFSA::StateIterator st = fsa_to->AddState(info);
      if(dfaset_to->isAccepting())
        fsa_to->setAccepting(st);
      else
        fsa_to->resetAccepting(st);
      dfa_list->push_front(*dfaset_to);
      stateMap->insert( std::pair<int, EncapFSA::State*>(dfaset_to->GetId(), &(*st)) );
      EncapFSA::StateIterator siter1 = fsa_to->GetStateIterator(state_from);
      if(et){
        EncapFSA::TransIterator tr_iter = fsa_to->AddTrans(siter1, st, *symbol_iter, false, PTraits<PFLTermExpression*>::One());  // FIXME last param (only a printing issue)
        (*tr_iter).setIncludingET(et);
        et->addEdgeRef(tr_iter);
      }
      else
        fsa_to->AddTrans(siter1, st, *symbol_iter, false, NULL);

      id = 0-dfaset_to->GetId();
    }
  return id;
}

//setInfo is false when we don't want that information are assigned to the states. An example id when we 
//are managing an automaton representing the Header Chain
EncapFSA* EncapFSA::NFAtoDFA(EncapFSA *orig, bool fieldExtraction, NodeList_t toExtract, bool setInfo)
{ 
  // PRINT_DOT(this,"outputting NFA2DFA begin","nfa2dfa_begin");
 
	std::list<DFAset> dlist;
	std::map<int, EncapFSA::State*> stateMap;

	//stack usato dall'algortimo
	std::list<DFAset*> dfaStack;

	//fase preliminare
	EncapFSA *fsa = new EncapFSA(orig->m_Alphabet); //the two automata have the same alphabet
	fsa->MergeCode1(orig->m_code1);
	fsa->MergeCode2(orig->m_code2);
	EncapFSA::State *init = &(*(orig->GetInitialState()));
	DFAset* dset = new DFAset();
	dset->AddState(init);
	E_Closure(dset, orig);
	UnsetVisited(orig); 
	AddSymToDFAset(dset, orig);
	dset->AutoSetInfo();
	//dset->parseAcceptingm_m_Status();
	dfaStack.push_front(dset);
		
	EncapFSA::StateIterator stateIt = fsa->AddState(dset->GetInfo()); //GetInfo() returns a SymbolProto. In this case returns NULL
	if(dset->isAccepting())
          fsa->setAccepting(stateIt);
	fsa->SetInitialState(stateIt);

	dlist.push_front(*dset);
	std::pair<int, EncapFSA::State*> p = make_pair<int, EncapFSA::State*>(dset->GetId(), &(*stateIt));
	stateMap.insert(p);

	while(dfaStack.size()>0)
	{
          PRINT_DOT(fsa, "nfa2dfa extloop start", "nfa2dfa_extloop_s");

		std::list<DFAset*>::iterator it = dfaStack.begin();
		DFAset *ds = *it;
		dfaStack.pop_front();
                EncapFSA::State *fromState = (stateMap.find(ds->GetId()))->second;
                EncapFSA::Alphabet_t symSet = ds->GetSymbols();
                EncapFSA::Alphabet_t::iterator i = symSet.begin();
		
		while(i != symSet.end()) //iterats over the symbols of the current DFAset
		{		
                  // loop on all symbols
                  DFAset* newds = new DFAset();
                  ExtendedTransition *et = NULL;
                  std::map<DFAset*, State*> et_out_gates;
                  bool complset_seen;
                  bool et_encountered = MoveExt(ds, orig, *i, newds, &et, &et_out_gates, &complset_seen); 
                  
                  if (et_encountered){
                    /* maps each of the token values received by the MoveExt
                     * to the actual states in the new fsa
                     */                     
                    std::map<State*, State*> updated_gates;

                    for(std::map<DFAset*, State*>::iterator gate_iter =
                          et_out_gates.begin();
                        gate_iter != et_out_gates.end();
                        gate_iter++){
                      DFAset *unionset=new DFAset(gate_iter->first, newds); // read MoveExt() comment
                      int id;
                      if ( (id=HandleSingleMovement(orig, fsa, fromState, unionset, i, &dlist, &stateMap, et,(!setInfo)? !complset_seen : false)) <0 )
                        dfaStack.push_front(unionset);

                      std::map<int, EncapFSA::State*>::iterator newstate = stateMap.find(abs(id));
                      nbASSERT(newstate != stateMap.end(),
                               "target new state should have been added into the state map by HandleSingleMovement()");
                      updated_gates.insert(std::pair<State*, State*>
                                           (gate_iter->second, newstate->second));
                    }
                    PRINT_ET_DOT(et,"after handling single movement", "after_move_handling");
                    et->changeGateRefs(fsa, fromState, updated_gates);
                  }
                  else
                    if( HandleSingleMovement(orig, fsa, fromState, newds, i, &dlist, &stateMap, NULL,setInfo) <0)
                      dfaStack.push_front(newds);

                  i++;
		} // end: while(i != symSet.end())

                PRINT_DOT(fsa, "nfa2dfa intloop end", "nfa2dfa_intloop_e");

		//complement
		EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto *>(NULL, NULL);
		DFAset* newds = new DFAset();
		Move(ds, orig, label, true, newds);
		E_Closure(newds, orig);
		UnsetVisited(orig);
		std::map<uint32,EncapFSA::State*> newdsStates = newds->GetStates();
		if(newdsStates.size() < 1)
			continue;
		int id;
		if((id = ExistsDFAset(dlist,newds)) > 0)
		{
			EncapFSA::State *st = (stateMap.find(id))->second;
			EncapFSA::StateIterator iterFrom = fsa->GetStateIterator(fromState);
			EncapFSA::StateIterator iterTo = fsa->GetStateIterator(st);
			EncapFSA::Alphabet_t symSet = ds->GetSymbols();
			fsa->AddTrans(iterFrom, iterTo, symSet, true, NULL);
		}
		else
		{
			AddSymToDFAset(newds, orig);
			newds->AutoSetInfo();
			// newds->parseAcceptingm_m_Status();
			SymbolProto* info = newds->GetInfo();
			EncapFSA::StateIterator st = fsa->AddState(info);
			if(newds->isAccepting())
                          fsa->setAccepting(st);
                        else
                          fsa->resetAccepting(st);
			dlist.push_front(*newds);
			std::pair<int, EncapFSA::State*> p = make_pair<int, EncapFSA::State*>(newds->GetId(), &(*st));
			stateMap.insert(p);
			EncapFSA::Alphabet_t symSet = ds->GetSymbols();
			EncapFSA::StateIterator iterFrom = fsa->GetStateIterator(fromState);
			fsa->AddTrans(iterFrom, st, symSet, true, NULL);
			dfaStack.push_front(newds);
		}
                PRINT_DOT(fsa, "nfa2dfa extloop end", "nfa2dfa_extloop_e");
	}
	
	if(setInfo)
	{
        PRINT_DOT(fsa, "nfa2dfa end before setfinalstates", "nfa2dfa_end_almost");
        fsa->fixStateProtocols();
        fsa->setFinalStates(fieldExtraction, toExtract);
    }
    
    PRINT_DOT(fsa, "nfa2dfa end", "nfa2dfa_end");
        
	return fsa;
}




void EncapFSA::BooleanNot()
{
	EncapFSA::StateIterator s = this->FirstState();
	while(s != this->LastState())
	{
          if((*s).isAccepting())
            this->resetAccepting(s);
          else
            this->setAccepting(s);

          s++;
	}
}


/* This method is the last shore to try and fix the states in the FSA
 * that we failed to associate with a protocol. This happens often
 * when the determinization is rather complex. This operation will
 * not be performed on final, non-accepting states.
 *
 * Needed by the code generation routines.
 */
void EncapFSA::fixStateProtocols(){
  EncapFSA::StateIterator s = this->FirstState();
  for (; s != this->LastState(); ++s){
    if ( ((*s).isFinal() && !(*s).isAccepting())
        || (*s).GetInfo() != NULL)
      continue;

    SymbolProto *proto_from_tr = NULL;
    for (EncapFSA::TransIterator t = this->FirstTrans();
         t != this->LastTrans() ;
         ++t){
      if ( (*(*t).ToState()) != *s)
        continue;

      // grab all labels from incoming transitions
      Alphabet_t labels = (*t).GetInfo().first;

      // try and extract the SymbolProto associated with the current state
      for (Alphabet_t::iterator l = labels.begin();
           l != labels.end();
           ++l)
      {
        if(proto_from_tr) {
          if (proto_from_tr != (*l).second) {
            // nothing can be done: incoming transitions with different destination protocols
            proto_from_tr = NULL;
            break;
          }
        }
        else
          proto_from_tr = (*l).second;
      }

      if (proto_from_tr == NULL && labels.size() > 0)
        // the current transition encountered some inconsistencies, this state cannot be uniquely identified
        break;
    }

    if(proto_from_tr)
      (*s).SetInfo(proto_from_tr);
  }
}

/*
 * Sets the 'final' flag on states that have no transitions leading to other states.
 * Compacts final states in a single couple (one accepting, the other not accepting).
 */
void EncapFSA::setFinalStates(bool fieldExtraction, NodeList_t toExtract){
  set<State*> final_accepting_states;
  set<State*> final_notaccepting_states;

  EncapFSA::StateIterator s = this->FirstState();
  while(s != this->LastState()){
    EncapFSA::TransIterator t = this->FirstTrans();
    bool loop = false;
    int transCount = 0;
    while(t != this->LastTrans()){
      if((*(*t).FromState()) == *s){
        if((*(*t).ToState()) == *s){
          loop = true;
        }
        transCount++;
      }
      t++;
    }
    
    if(loop && transCount == 1){
      this->setFinal(s);
      if((*s).isAccepting()) {
        final_accepting_states.insert(&(*s));
        this->setAccepting(s);
      }
      else
        final_notaccepting_states.insert(&(*s));
    }
    s++;
  }

  map<State*, State*> trasformation_map;

	//we have to compact those final accepting state which are not on paths to protocols related to the field extraction (in case of we are doing field extraction)

  if(final_accepting_states.size() > 1) {
    State *tmp_s = NULL;
    for(set<State*>::iterator i = final_accepting_states.begin();
        i != final_accepting_states.end();
        ++i) {
      if(fieldExtraction&& (!checkIfInsert(toExtract,(*i)->GetInfo())))
      	continue;
      if (tmp_s)
      {
      	trasformation_map[*i] = tmp_s;
      }
      else
        tmp_s = *i;
    }
  }

  if(final_notaccepting_states.size() > 1) {
    State *tmp_s = NULL;
    for(set<State*>::iterator i = final_notaccepting_states.begin();
        i != final_notaccepting_states.end();
        ++i) {
      if (tmp_s)
        trasformation_map[*i] = tmp_s;
      else
        tmp_s = *i;
    }
  }

  PRINT_DOT(this, "almost at the end of statefix", "statefix_almost_end");

  if( trasformation_map.size() > 0 )
    compactStates(trasformation_map, true);
}

bool EncapFSA::checkIfInsert(NodeList_t toExtract, SymbolProto *protocol)
{
	if(protocol==NULL)
		return true;
	
	bool toRet = true;
	EncapGraph::GraphNode *node = m_ProtocolGraph->GetNode(protocol);
	for(NodeList_t::iterator iter = toExtract.begin(); iter != toExtract.end(); iter++)
	{
		EncapGraph::GraphNode *toExtractNode = *iter;
		if(m_ProtocolGraph->ExistPaths(*toExtractNode, *node))
		{
			toRet = false;
			break;
		}
		
	}
	return toRet;
}

/***************************** Print automata *****************************/

void EncapFSA::prettyDotPrint(EncapFSA *fsa, char *description, char *dotfilename, char *src_file, int src_line) {
  static int dbg_incremental_id = 0;

  ++dbg_incremental_id;

  if (src_file != NULL && src_line != -1){
    cerr << setfill('0');
    cerr << description << ": " << setw(3) << dbg_incremental_id << " (@ "
	 << src_file << " : " << src_line << ")" << endl;
  }

  std::stringstream myconv;
  myconv << setfill('0');
  myconv << setw(3) << dbg_incremental_id;
  std::string fname = string("dbg_") + myconv.str() +
    string("_") + string(dotfilename) + string(".dot");
  ofstream outfile(fname.c_str());

  sftDotWriter<EncapFSA> fsaWriter(outfile);
  fsaWriter.DumpFSA(fsa);
}

/***************************** SYMBOL CODE *****************************/
void EncapFSA::AddCode1(EncapLabel_t label,string code)
{
	m_code1.insert(make_pair<SymbolProto*,string>(label.second,code));
}

void EncapFSA::AddCode2(EncapLabel_t label,string code)
{
	m_code2.insert(make_pair<SymbolProto*,string>(label.second,code));
}


void EncapFSA::MergeCode1(Code_t first,Code_t second)
{
	
	for(Code_t::iterator it = first.begin(); it != first.end(); it++)
		m_code1.insert(*it);
	for(Code_t::iterator it = second.begin(); it != second.end(); it++)
		m_code1.insert(*it);
}


void EncapFSA::MergeCode1(Code_t code)
{
	for(Code_t::iterator it = code.begin(); it != code.end(); it++)
		m_code1.insert(*it);
}

void EncapFSA::MergeCode2(Code_t first,Code_t second)
{
	
	for(Code_t::iterator it = first.begin(); it != first.end(); it++)
		m_code2.insert(*it);
	for(Code_t::iterator it = second.begin(); it != second.end(); it++)
		m_code2.insert(*it);
}


void EncapFSA::MergeCode2(Code_t code)
{
	for(Code_t::iterator it = code.begin(); it != code.end(); it++)
		m_code2.insert(*it);
}


void EncapFSA::AddVariable1(EncapLabel_t label, SymbolTemp *var)
{
	m_variable1.insert(make_pair<SymbolProto*,SymbolTemp*>(label.second,var));
}

void EncapFSA::AddVariable2(EncapLabel_t label, SymbolTemp *var)
{
	m_variable2.insert(make_pair<SymbolProto*,SymbolTemp*>(label.second,var));
}


bool EncapFSA::HasCode(EncapLabel_t l)
{
	SymbolProto *p = l.second;
	
	//check m_code1
	for(Code_t::iterator it = m_code1.begin(); it != m_code1.end(); it++)
	{
		if((*it).first == p)
			return true;
	}
	
	//check m_code2
	for(Code_t::iterator it = m_code2.begin(); it != m_code2.end(); it++)
	{
		if((*it).first == p)
			return true;
	}

	return false;
}

string EncapFSA::GetCode1(EncapLabel_t label)
{
	SymbolProto *protocol = label.second;
	for(Code_t::iterator it = m_code1.begin(); it != m_code1.end(); it++)
	{
		if((*it).first == protocol)
			return (*it).second;
	}
	nbASSERT(false,"You cannot be here! you found a BUG!");
}

string EncapFSA::GetCode2(EncapLabel_t label)
{
	SymbolProto *protocol = label.second;
	for(Code_t::iterator it = m_code2.begin(); it != m_code2.end(); it++)
	{
		if((*it).first == protocol)
			return (*it).second;
	}
	nbASSERT(false,"You cannot be here! you found a BUG!");
}


list<SymbolTemp*> EncapFSA::GetVariables(EncapLabel_t symbol)
{
	list<SymbolTemp*> variables;
	
	//check m_variable1
	for(Variable_t::iterator it = m_variable1.begin(); it != m_variable1.end(); it++)
	{
		if((*it).first == symbol.second)
		{
			variables.push_back((*it).second);
			break;
		}	
	}
	
	//chack m_variable2
	for(Variable_t::iterator it = m_variable2.begin(); it != m_variable2.end(); it++)
	{
		if((*it).first == symbol.second)
		{
			variables.push_back((*it).second);
			break;
		}	
	}
	
	return variables;
}

SymbolTemp* EncapFSA::GetVariable1(EncapLabel_t symbol)
{
	for(Variable_t::iterator it = m_variable1.begin(); it != m_variable1.end(); it++)
	{
		if((*it).first == symbol.second)
			return (*it).second;
	}
	return NULL;
}

SymbolTemp* EncapFSA::GetVariable2(EncapLabel_t symbol)
{
	for(Variable_t::iterator it = m_variable2.begin(); it != m_variable2.end(); it++)
	{
		if((*it).first == symbol.second)
			return (*it).second;
	}
	return NULL;
}



/***************************** Field extraction *****************************/

EncapFSA::StateIterator EncapFSABuilder::AddState(EncapFSA &fsa, EncapGraph::GraphNode &origNode, bool forceInsertion)
{
	NodeMap_t::iterator i = m_NodeMap.find(&origNode);
	if (i == m_NodeMap.end() || forceInsertion)
	{
          EncapStIterator_t newState = fsa.AddState(origNode.NodeInfo);
          if ( i == m_NodeMap.end() ) { // add the state to the caching map only if it is the first one associated with this origNode
            pair<EncapGraph::GraphNode*, EncapStIterator_t> p = make_pair<EncapGraph::GraphNode*, EncapStIterator_t>(&origNode, newState);
            m_NodeMap.insert(p);
          }
          return newState;
	}

	return i->second;
}

EncapFSABuilder::EncapFSABuilder(EncapGraph &protograph, EncapFSA::Alphabet_t &alphabet)
	:m_Graph(protograph), m_Alphabet(alphabet), m_Status(nbSUCCESS)
{
}

//[icerrato] needed to implement the field extraction
EncapFSA *EncapFSABuilder::BuildSingleNodeFSA()
{
	//this automaton is built when the filtering expression has not been specified
	//initial accepting state
	EncapFSA *fsa = new EncapFSA(m_Alphabet);
	EncapGraph::GraphNode &startNode = m_Graph.GetStartProtoNode();
	EncapStIterator_t newState = this->AddState(*fsa, startNode, false);   
	fsa->SetInitialState(newState); 
	fsa->setAccepting(newState);  
	EncapFSA::Alphabet_t empty;
	fsa->AddTrans(newState, newState, empty, true, NULL);	
	
	//this automaton hasn't the final not accepting state
	
	PRINT_DOT(fsa, "before BuildSingleNodeFSA return", "BuildSingleNodeFSA");     
	return fsa;
}

//[icerrato] needed to implement the field extraction
EncapFSA *EncapFSABuilder::ExtractFieldsFSA(NodeList_t &toExtract, EncapFSA *fsaFE)
{
	map<SymbolProto*,EncapFSA::State*>states; 						//this map correlates the states of this part with their protocols
	set<SymbolProto*>protocols;										//this set contains the protocols related to the last state of the automaton
	map<EncapGraph::GraphNode*,set<EncapLabel_t> > labelsFound;		//contains the labels of the transitions exiting from a state and related to a protocol
	map<EncapFSA::State*,EncapFSA::Transition*>transitionsToDummy;	//this map correlated the states of this part with their transitions to the dummy final accepting state
	
	EncapFSA::Alphabet_t empty; //empty alphabet
	
	//we create a dummy final accepting state
	EncapStIterator_t dummyState = fsaFE->AddState(NULL);
	fsaFE->setFinal(dummyState);

	//we obtain all the accepting states of the automaton related to the filter
	std::list<EncapFSA::State*> stateList = fsaFE->GetAcceptingStates();	
	std::list<EncapFSA::State*>::iterator i = stateList.begin();
	for(i = stateList.begin() ; i != stateList.end(); ++i)
	{
		EncapFSA::State *lastState = *i;
		SymbolProto *lastProto = lastState->GetInfo();	
		//we have to get the symbols related to the current accepting states of the EncapFSA
		EncapStIterator_t lastStateIt = fsaFE->GetStateIterator(lastState);	
		fsaFE->resetFinal(lastStateIt);
		//now lastState is still an accepting state, but not an accepting final state	
		//we must link the current last state of the automaton related to the filter with the final dummy final accepting state
		EncapTrIterator_t lastToDummy = fsaFE->AddTrans(lastStateIt, dummyState, empty, false, NULL);
		transitionsToDummy[lastState] = &(*lastToDummy); //store the transition from the last state to the dummy final accepting state
		//we must remove the self transition on the last state of the automaton related to the filter
		list<EncapFSA::Transition*> transitions = fsaFE->getTransEnteringTo(lastState);
		for(list<EncapFSA::Transition*>::iterator it = transitions.begin(); it != transitions.end();it++)
		{
			if((*it)->FromState()==(*it)->ToState())
			{
				(*it)->RemoveEdge();
				delete(*it);
				break;
			}
		}
		set<EncapLabel_t> sl = FindLabels(lastState,fsaFE);
		if(lastProto!=NULL)
		{
			//this state is related to a single protocol
			states[lastProto]=lastState;
			protocols.insert(lastProto);
			EncapGraph::GraphNode *node = m_Graph.GetNode(lastProto);
			labelsFound[node]=sl;
		}
		else
		{
			continue; //this state is a final accepting state related to more than one protocol. No one of these protocols is on a path for field extraction because a protocol on a path to a field extaction is on a final accepting state only related to it
		}

	}
	
	//we iterate over the protocols of the last state of the automaton related to the filter
	for(set<SymbolProto*>::iterator pIt = protocols.begin(); pIt != protocols.end(); pIt++)
	{	
		//we must obtain the node related to the protocol of lastState
		EncapGraph::GraphNode *targetNode = m_Graph.GetNode(*pIt);
		//now we must iterate over the nodes related to the field extraction, in order to create the paths
		for(NodeList_t::iterator iter = toExtract.begin(); iter != toExtract.end(); iter++)
		{
			EncapGraph::GraphNode *toExtractNode = *iter;
			if(m_Graph.ExistPaths(*toExtractNode, *targetNode))
			{
				//a path has been found and we iterate over its nodes
				for (EncapGraph::OnPathIterator n = m_Graph.FirstNodeOnPath(); n != m_Graph.LastNodeOnPath(); n++)
				{
					//create/obtain the state for this protocol
					SymbolProto *currentProto = (*n)->NodeInfo;
					EncapFSA::State *currentState = GetStateFromProto(&states, fsaFE, currentProto, *n, &transitionsToDummy,dummyState,&labelsFound);
					EncapStIterator_t currentStateIt =  fsaFE->GetStateIterator(currentState);
					//we must get the incoming edges to the node n
					multimap<EncapGraph::GraphNode*,EncapGraph::GraphEdge> incomingEdges = m_Graph.GetMapNodeEdges();
					pair<multimap<EncapGraph::GraphNode*,EncapGraph::GraphEdge>::iterator, multimap<EncapGraph::GraphNode*,EncapGraph::GraphEdge>::iterator> edges;
					edges = incomingEdges.equal_range(*n);
					//we iterate over the incaming edges of the node n
					for (multimap<EncapGraph::GraphNode*,EncapGraph::GraphEdge>::iterator eIt = edges.first; eIt != edges.second; ++eIt)
					{
						//create/obtain the state for this protocol
						SymbolProto *fromProto = (*eIt).second.From.NodeInfo;
						EncapGraph::GraphNode *fromNode = &((*eIt).second.From);
						EncapFSA::State *fromState = GetStateFromProto(&states, fsaFE, fromProto, fromNode, &transitionsToDummy,dummyState,&labelsFound);
						EncapStIterator_t fromStateIt =  fsaFE->GetStateIterator(fromState);
						//create the label
						EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(fromProto, currentProto);
						set<EncapLabel_t>labels = (labelsFound.find(fromNode))->second;		
						//add the transition
						if(labels.count(label)==0)
						{
							fsaFE->AddTrans(fromStateIt, currentStateIt, label, false, NULL);
							//store the label
							labels.insert(label);
						}
						labelsFound[fromNode] = labels;
					}
				}
			}
		}
		PRINT_DOT(fsaFE, "ExtractFieldsFSA after a target of the filter", "extractfieldsfsa_after_a_target_of_filter");	
	}
	
	
	//we have to put the labels on the transition to the dummy final accepting state
	AddLabels(labelsFound, transitionsToDummy,states);
	
	RemoveUselessTransitions(states,dummyState,fsaFE);
	
	//add self transitions to states without an exiting one
	AddSelfTransitions(states,fsaFE);
	
	//verify if the dummy state is useful and, if this is not the case, remove it. Otherwise, it is set as accepting state and a self transition is added
	list<EncapFSA::Transition*> listTr = fsaFE->getTransEnteringTo(&(*dummyState));
	if(listTr.size()==0)
	{
		//the dummy state is useless
		fsaFE->removeState(&(*dummyState));
	}
	else
	{
		fsaFE->AddTrans(dummyState, dummyState, empty, true, NULL);
		fsaFE->setAccepting(dummyState);
	}

	PRINT_DOT(fsaFE, "before ExtractFieldsFSA return", "extractfieldsfsa");

	return fsaFE;
}

//[icerrato] needed to implement the field extraction
void EncapFSABuilder::AddSelfTransitions(map<SymbolProto*,EncapFSA::State*>states, EncapFSA *fsa)
{
	Alphabet_t empty;
	for(map<SymbolProto*,EncapFSA::State*>::iterator s = states.begin(); s != states.end(); s++)
	{
		EncapFSA::State *state = (*s).second;
		list<EncapFSA::Transition*> transitions = fsa->getTransExitingFrom(state);
		if(transitions.size()==0)
		{
			EncapStIterator_t stateIt =  fsa->GetStateIterator(state);
			fsa->AddTrans(stateIt, stateIt, empty, true, NULL);
			fsa->setFinal(stateIt);
		}
	}
}

//[icerrato] needed to implement the field extraction
set<EncapLabel_t> EncapFSABuilder::FindLabels(EncapFSA::State *lastState, EncapFSA *fsa)
{
	set<EncapLabel_t> sl;
	list<EncapFSA::Transition*> transitions = fsa->getTransExitingFrom(lastState);
	for(list<EncapFSA::Transition*>::iterator it = transitions.begin(); it != transitions.end();it++)
	{
		Alphabet_t alph = (*it)->GetLabels();
		for(Alphabet_t::iterator l = alph.begin(); l!=alph.end(); l++ )
			sl.insert(*l);
	}
	
	return sl;
}

//[icerrato] needed to implement the field extraction
EncapFSA::State *EncapFSABuilder::GetStateFromProto(map<SymbolProto*,EncapFSA::State*> *states, EncapFSA *fsa,SymbolProto *protocol, EncapGraph::GraphNode *node, map<EncapFSA::State*,EncapFSA::Transition*> *transitionsToDummy, EncapStIterator_t dummyState,map<EncapGraph::GraphNode*,set<EncapLabel_t> > *labelsFound)
{
	if(states->count(protocol)!=0)
	{
		EncapFSA::State *state =  (states->find(node->NodeInfo))->second;		
		return state;
	}
	else
	{
		//we have to create the state, set it as accepting state and link it to the dummy final accepting state
		EncapStIterator_t currentStateIt = this->AddState(*fsa, *node,true);  //clones are allowed
		EncapFSA::State *state = &(*currentStateIt); 
		states->insert(pair<SymbolProto*,EncapFSA::State*>(node->NodeInfo,state));    
		EncapFSA::Alphabet_t empty;
		EncapTrIterator_t toDummy = fsa->AddTrans(currentStateIt, dummyState, empty, false, NULL);
		transitionsToDummy->insert(pair<EncapFSA::State*,EncapFSA::Transition*>(state,&(*toDummy)));
		fsa->setAccepting(currentStateIt);
		set<EncapLabel_t> ls;
		labelsFound->insert(pair<EncapGraph::GraphNode*,set<EncapLabel_t> >(node,ls));
		return state;
		
	}
}

//[icerrato] Needed to implement the field extraction
void EncapFSABuilder::AddLabels(map<EncapGraph::GraphNode*,set<EncapLabel_t> > labelsFound, map<EncapFSA::State*,EncapFSA::Transition*>transitionsToDummy, map<SymbolProto*,EncapFSA::State*>states)
{
	//this method adds on the transition to the dummy state, all those labels related to the considered state which are in the protocol encapsulation graph but not on the transitions exiting
	//from the considered state
	map<EncapGraph::GraphNode*,set<EncapLabel_t> >::iterator l = labelsFound.begin();
	for(;l != labelsFound.end();l++)
	{
	
		EncapGraph::GraphNode *node = (*l).first;
		SymbolProto *protocol = node->NodeInfo;
		
		EncapFSA::State *state = (states.find(protocol))->second;
		EncapFSA::Transition *tr = (transitionsToDummy.find(state))->second;
		set<EncapLabel_t> found = (labelsFound.find(node))->second;	
		
		list<EncapGraph::GraphNode*> &successors = (node)->GetSuccessors();//get the predecessors of the current node
		//iterate over the predecessors
		for (list<EncapGraph::GraphNode*>::iterator s = successors.begin(); s != successors.end();s++)
		{		
			EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(node->NodeInfo, (*s)->NodeInfo);
			//if this label is not in the set, we add it on the transition to the dummy state
			if(found.count(label)==0)
				tr->AddLabel(label);
		}
	}

}

//[icerrato] Needed to implement the the field extraction
void EncapFSABuilder::RemoveUselessTransitions(map<SymbolProto*,EncapFSA::State*>states, EncapStIterator_t dummyState, EncapFSA *fsa)
{
	//if a transition to the dummy state has no labels, or it is the only one exiting from a state, it is useless and can be removed
	list<EncapFSA::Transition*> transitions = fsa->getTransEnteringTo(&(*dummyState));
	for(list<EncapFSA::Transition*>::iterator tr = transitions.begin(); tr != transitions.end(); )
	{
		list<EncapFSA::Transition*>::iterator aux = tr;
		EncapFSA::Alphabet_t alphabet = (*tr)->GetLabels();		
		tr++;
		EncapStIterator_t from = (*aux)->FromState();
		list<EncapFSA::Transition*> extTr = fsa->getTransExitingFrom(&(*from));
		if(((alphabet.size()==0)&&(!(*aux)->IsComplementSet())) || (extTr.size()==1))
		{
			(*aux)->RemoveEdge();
			delete(*aux);
		}
	}
}

/***************************** Automata builder *****************************/

/*
*	Note that the target cannot be a set or the any placeholder, and cannot have a repeat operator
*/
//FIXME: startproto in un set ha senso? 
//FIXME: il filtro "startproto" va in segmentation fault
//FIXME: implementare tunneled

//create the FSA related to a regular expression
EncapFSA *EncapFSABuilder::BuildRegExpFSA(list<PFLSetExpression*>* innerList)
{	
	EncapFSA *fsa = new EncapFSA(m_Alphabet);
	
	EncapFSA::Alphabet_t empty; 		//empty alphabet
	
	set<SymbolProto*>allProtos; //contains each symbol of the alphabet
	EncapFSA::Alphabet_t completeAlphabet = CompleteAlphabet(&allProtos);	//aphabet made up of all the possible symbols
	
	EncapFSA::State *previousState;	
	set<SymbolProto*>previousProtos; //stores the protocols related to the last state built
	
	bool firstStart = StartAsFirst(innerList);
	if(!firstStart)
	{
		//the first element of the regexp is not startproto
		EncapStIterator_t eatAllState = fsa->AddState(NULL);		
		fsa->AddTrans(eatAllState, eatAllState, completeAlphabet, false, NULL);
		fsa->SetInitialState(eatAllState);
		previousState = &(*eatAllState);
		previousProtos.insert(allProtos.begin(),allProtos.end()); 
		//the just created state represents the element .*, hence it is related to any protocol of the database
		PRINT_DOT(fsa, "buildregexpfsa - before header chain", "buildregexpfsa_before_chain");
	}
	else
	{
		//the first element of the regexp is startproto
		EncapStIterator_t startState = fsa->AddState(NULL);
		fsa->SetInitialState(startState);
		previousState = &(*startState);
		previousProtos.insert(m_Graph.GetStartProtoNode().NodeInfo);
		//the just created state represents the element startproto
		PRINT_DOT(fsa, "buildregexpfsa - after first element", "buildregexpfsa_after_startproto");
	}

	
	bool all_ok = true; //it is false when we understand that the regexp can never be verified, in agreement with the protocol encapsulation graph
	
	EncapFSA::State *newState = NULL;
	
	//get an iterator over the elements of the header chain
	list<PFLSetExpression*>::reverse_iterator it = innerList->rbegin();
	
	if(firstStart)
		it++;
    	
    AssignProtocolLayer assigner(m_Graph);//needed in case of tunneled
	
	for(; it != innerList->rend(); it++)
	{
		PFLInclusionOperator inclusionOp = (*it)->GetInclusionOperator(); 	//DEFAULT - IN - NOTIN
		PFLRepeatOperator repeatOp = (*it)->GetRepeatOperator();			//DEFAULT_ROP - PLUS - STAR - QUESTION 
		bool any = ((*it)->IsAnyPlaceholder())? true : false;
		set<SymbolProto*>currentProtos; 
		set<pair<set<SymbolProto*>,PFLTermExpression*> > currentPredicates;
		if(any)
		{
			//in any -> .
			//the current element is the any placeholder, hence it is related to any protocol
			//the any placeholder can be involved only in the "in" operation. the grammar forbids "notin any"
			currentProtos.insert(allProtos.begin(),allProtos.end());
		}
		else
		{
			SymbolProto *protocol = NULL;			
			list<PFLTermExpression*> *elements = (*it)->GetElements(); //get the protocols specified in the current set
			list<PFLTermExpression*>::iterator eIt = elements->begin();
			
			/*
			* Note that, nevertheless the grammar allows it, a protocol can appear a single time within the same set
			*/
			
			if(inclusionOp==NOTIN)
			{
				//notin {p1,p2,...} -> [^p1 p2 ...]
				//the current element could have a repeat operator, the header indexing, a predicate and/or the tunneled. remember that
				//an element cannot have both the header indexing and a repeat operator
				//the current element is related to any protocol except those specified by the notin
				currentProtos.insert(allProtos.begin(),allProtos.end());
				//iterate over the elements of the current set
				set<SymbolProto*>found;
				for(;eIt != elements->end();eIt++)
				{
					protocol = (*eIt)->GetProtocol(); 	//get the protocol
					if(found.count(protocol)!=0)
					{
						//syntax error!
						m_ErrorRecorder.PFLError("syntax error - in a single set, a protocol cannot appear more than one time.");
						return NULL;
					}
					found.insert(protocol);
				
					protocol = (*eIt)->GetProtocol();					
					if((*eIt)->GetHeaderIndex()>0)
					{
						if(repeatOp != DEFAULT_ROP)
						{
							//syntax error!
							m_ErrorRecorder.PFLError("syntax error - you cannot specify a repeat operator and the header indexing on the same element.");
							return NULL;
						}
					
						//this element requires the header indexing. Then all the symbol leading to its protocol must have the code to increment a counter
						EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, protocol);//label representing all the symbols leading to proto
						fsa->AddCode1(label,protocol->Name+".HeaderCounter++");
					}
					
					if((*eIt)->MandatoryTunnels())
					{
						//tunneled
						assigner.Classify(); //assign a layer to each protocol of the protocol encapsulation graph
						set<SymbolProto*> protocols = assigner.GetProtocolsOfLayer(protocol->Level);
						for(set<SymbolProto*>::iterator sp = protocols.begin(); sp != protocols.end(); sp++)
						{
							//this protocol requires the counter for tunneled.
							EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, *sp);//label representing all the symbols leading to this protocol
							fsa->AddCode2(label,(*sp)->Name+".LevelCounter++");
						}
					}
						
					currentProtos.erase(protocol);
					if((*eIt)->GetIRExpr()!=NULL or ((*eIt)->GetHeaderIndex()!=0) or ((*eIt)->MandatoryTunnels()))
					{
						//this protocol has the predicate
						set<SymbolProto*> aux;
						aux.insert(protocol);
						currentPredicates.insert(make_pair<set<SymbolProto*>,PFLTermExpression*>(aux,*eIt));
					}
				}
			}
			else
			{
				//in {p1,p2,...} -> [p1 p2 ...]
				//the current element could have a repeat operator, the header indexing, a predicate and/or the tunneled. remember that
				//an element cannot have both the header indexing and a repeat operator
				//the current element is related to those protocols specified by the in
				set<SymbolProto*> found;
				for(;eIt != elements->end();eIt++)
				{
					protocol = (*eIt)->GetProtocol(); 	//get the protocol
					if(found.count(protocol)!=0)
					{
						//syntax error!
						m_ErrorRecorder.PFLError("syntax error - in a single set, a protocol cannot appear more than one time.");
						return NULL;
					}
					found.insert(protocol);
					
					if((*eIt)->GetHeaderIndex()>0)
					{
						if(repeatOp != DEFAULT_ROP)
						{
							//syntax error!
							m_ErrorRecorder.PFLError("syntax error - you cannot specify a repeat operator and the header indexing on the same element.");
							return NULL;
						}
					
						//this element requires the header indexing. Then all the symbol leading to its protocol must have the code to increment a counter
						EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, protocol);//label representing all the symbols leading to proto
						fsa->AddCode1(label,protocol->Name+".HeaderCounter++");
					}
					
					if((*eIt)->MandatoryTunnels())
					{
						//this element must be involved in a tunnel
						assigner.Classify(); //assign a layer to each protocol of the protocol encapsulation graph
						set<SymbolProto*> protocols = assigner.GetProtocolsOfLayer(protocol->Level);
						for(set<SymbolProto*>::iterator sp = protocols.begin(); sp != protocols.end(); sp++)
						{
							//this protocol requires the counter for tunneled.
							EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, *sp);//label representing all the symbols leading to this protocol
							fsa->AddCode2(label,(*sp)->Name+".LevelCounter++");
						}
					}
					
					if(((*eIt)->GetIRExpr()==NULL) and ((*eIt)->GetHeaderIndex()==0) and (!(*eIt)->MandatoryTunnels()))
						//this protocol is without predicate
						currentProtos.insert(protocol);
					else 
					{	
						//this protocol has the predicate. it could be on a field, on a counter, or on both
						set<SymbolProto*> aux;
						aux.insert(protocol);
						currentPredicates.insert(make_pair<set<SymbolProto*>,PFLTermExpression*>(aux,*eIt));
					}
				}
			}
		}
		//now, we know the protocols represented by the current state, and those represented by the previous state	
		newState = CreateLink(fsa,previousProtos,currentProtos,previousState,repeatOp,completeAlphabet,currentPredicates,inclusionOp);
		
		if(newState == NULL)
		{
			//the regexp cannot be matched in according to the referred encapsulation rules
			all_ok = false;
			break;
		}	
	
		if(repeatOp == PLUS or repeatOp == DEFAULT_ROP)
			//STAR and QUESTION, because the incoming epsilon transition, represent also the protocols of the previous state
			previousProtos.clear();

		previousProtos.insert(currentProtos.begin(),currentProtos.end());
		//we must consider also those protocols reached if a predicate is satisfied
		for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator prIt = currentPredicates.begin(); prIt != currentPredicates.end(); prIt++)
		{
			set<SymbolProto*> aux = (*prIt).first;
			previousProtos.insert(*(aux.begin()));
		}
		
		previousState = newState;
		
		PRINT_DOT(fsa, "buildregexpfsa - after the management of an element", "buildregexpfsa_after_element_loop");
	
	} //end iteration over the elements of the header chain
	
	//we have to set last state as the final accepting state, but only in case of the target of the chain can be reached
	if(all_ok)
	{	
		EncapStIterator_t lastState = fsa->GetStateIterator(*previousState);
		fsa->setAccepting(lastState);
		fsa->setFinal(lastState);
		fsa->AddTrans(lastState, lastState, empty, true, NULL);		
	}
	  
	PRINT_DOT(fsa, "buildregexpfsa - automaton completed!", "buildregexpfsa_end");
	return fsa;
}

//Create a label for each transition of the Protocol Encapsulation Graph, and store each protocol in a set
EncapFSA::Alphabet_t EncapFSABuilder::CompleteAlphabet(set<SymbolProto*> *allProtos)
{
	EncapFSA::Alphabet_t alphabet;
	m_Graph.SortRevPostorder_alternate(m_Graph.GetStartProtoNode());
	for (EncapGraph::SortedIterator i = m_Graph.FirstNodeSorted(); i != m_Graph.LastNodeSorted(); i++)
	{
		allProtos->insert((*i)->NodeInfo);
		list<EncapGraph::GraphNode*> &predecessors = (*i)->GetPredecessors();
		for (list<EncapGraph::GraphNode*>::iterator p = predecessors.begin();p != predecessors.end();p++)
		{
			EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>((*p)->NodeInfo, (*i)->NodeInfo);
			alphabet.insert(label);
		}				
	}
	return alphabet;
}

//links two states
//IVANO HATES THE PREDICATES!!!
EncapFSA::State *EncapFSABuilder::CreateLink(EncapFSA *fsa,set<SymbolProto*>previousProtos,set<SymbolProto*>currentProtos,EncapFSA::State *previousState,PFLRepeatOperator repeatOp,EncapFSA::Alphabet_t completeAlphabet,set<pair<set<SymbolProto*>,PFLTermExpression*> > currentPredicates, PFLInclusionOperator inclusionOp)
{
	switch(repeatOp)
	{ 
		case DEFAULT_ROP:
			return ManageDefaultROP(fsa,previousProtos,currentProtos,previousState,repeatOp,completeAlphabet,currentPredicates, inclusionOp,currentPredicates.size());
		case PLUS:
			return ManagePLUS(fsa,previousProtos,currentProtos,previousState,repeatOp,completeAlphabet,currentPredicates,inclusionOp,currentPredicates.size());	
		case STAR:
			return ManageSTAR(fsa,previousProtos,currentProtos,previousState,repeatOp,completeAlphabet,currentPredicates,inclusionOp,currentPredicates.size());
		case QUESTION:
			return ManageQUESTION(fsa,previousProtos,currentProtos,previousState,repeatOp,completeAlphabet,currentPredicates,inclusionOp,currentPredicates.size());	
		default:
			nbASSERT(false,"There is a BUG! You cannot be here!");
	}
	return NULL;//useless but the compiler wants it
}

//manage the case without the repeat operator
//this is the only case that support the header indexing
EncapFSA::State *EncapFSABuilder::ManageDefaultROP(EncapFSA *fsa,set<SymbolProto*>previousProtos,set<SymbolProto*>currentProtos,EncapFSA::State *previousState,PFLRepeatOperator repeatOp,EncapFSA::Alphabet_t completeAlphabet,set<pair<set<SymbolProto*>,PFLTermExpression*> > currentPredicates,PFLInclusionOperator inclusionOp,int predSize)
{
	EncapStIterator_t newState = fsa->AddState(NULL);
	EncapStIterator_t predecessorState = fsa->GetStateIterator(*previousState);
	
	bool link = false;

	//create the alphabet
	EncapFSA::Alphabet_t alphabet = CreateAlphabet(previousProtos,currentProtos,completeAlphabet);
	if(alphabet.size()==0)
	{
		if(predSize==0)
		{
			//we are sure that the transition will never fire
			fsa->removeState(&(*newState));
			return NULL;
		}
	}
	else
	{
		//add the transition
		fsa->AddTrans(predecessorState, newState, alphabet, false, NULL);		
		link = true;
	}
			
	//create the eventual extended transitions
	if(predSize!=0)
	{
		EncapStIterator_t failState = fsa->AddState(NULL);//create a fail state
	
		for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator it = currentPredicates.begin(); it != currentPredicates.end(); it++)
		{
			nbASSERT(((*it).first).size()==1,"There is a bug. We use a set only for conveniente, but there is only one protocol");
			PFLTermExpression *currentPEx = (*it).second;
			EncapFSA::Alphabet_t extAlphabet = CreateAlphabet(previousProtos,(*it).first,completeAlphabet);
			if(extAlphabet.size()!=0)
			{
				link = true;
				for(EncapFSA::Alphabet_t::iterator aIt = extAlphabet.begin(); aIt != extAlphabet.end(); aIt++)
				{
					//this predicate is related to a protocol field, to a counter or both
					
					if(currentPEx->GetHeaderIndex()==0 and !currentPEx->MandatoryTunnels())
						//predicate on a protocol field
						fsa->addExtended(predecessorState, *aIt, (inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,currentPEx);
					else if((currentPEx->GetHeaderIndex()!=0 and !currentPEx->MandatoryTunnels()) or (currentPEx->GetHeaderIndex()==0 and currentPEx->MandatoryTunnels()))
					{
						//predicate on a counter and eventually on a protocol field
						SymbolProto *p = *(((*it).first).begin());
						uint32 value = (currentPEx->GetHeaderIndex()!=0)? currentPEx->GetHeaderIndex() : 2;
						RangeOperator_t op = (currentPEx->GetHeaderIndex()!=0)? EQUAL : GREAT_EQUAL_THAN;
						EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, p);//label representing all the symbols leading to proto
						string code = (currentPEx->GetHeaderIndex()!=0)? fsa->GetCode1(label) : fsa->GetCode2(label);
						code.resize(code.size()-2);
						fsa->addExtended(predecessorState, *aIt,(inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,value,op,code,(currentPEx->GetIRExpr())?currentPEx:NULL); 
					}
					else
					{
						//predicate on two counters and eventually on a protocol field
						SymbolProto *p = *(((*it).first).begin());
						EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, p);//label representing all the symbols leading to proto
						//prepare the header indexing
						uint32 indexing = currentPEx->GetHeaderIndex();
						RangeOperator_t opIndex = EQUAL;
						string codeIndex = fsa->GetCode1(label);
						codeIndex.resize(codeIndex.size()-2);
						//prepare the tunneled
						uint32 tunneled = 2;
						RangeOperator_t opTunneled = GREAT_EQUAL_THAN;
						string codeTunneled = fsa->GetCode2(label);
						codeTunneled.resize(codeTunneled.size()-2);
						//create the extended transition - provide to the method before the parameters related to tunneled
						fsa->addExtended(predecessorState, *aIt,(inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,
												tunneled,opTunneled,codeTunneled,indexing,opIndex,codeIndex,(currentPEx->GetIRExpr())?currentPEx:NULL);
					}
				} 
			}
		}

		if(!link)
		{
			//the regular expression will never be mathced!
			fsa->removeState(&(*newState));
			fsa->removeState(&(*failState));
			return NULL;
		}
	}
	return &(*newState);
}

//manage the repeat operator +
EncapFSA::State *EncapFSABuilder::ManagePLUS(EncapFSA *fsa,set<SymbolProto*>previousProtos,set<SymbolProto*>currentProtos,EncapFSA::State *previousState,PFLRepeatOperator repeatOp,EncapFSA::Alphabet_t completeAlphabet,set<pair<set<SymbolProto*>,PFLTermExpression*> > currentPredicates,PFLInclusionOperator inclusionOp,int predSize)
{
	EncapStIterator_t newState = fsa->AddState(NULL);
	EncapStIterator_t predecessorState = fsa->GetStateIterator(*previousState);
	
	bool link = false;

	//create the alphabet
		
	EncapFSA::Alphabet_t alphabet = CreateAlphabet(previousProtos,currentProtos,completeAlphabet);
	if(alphabet.size()==0)
	{
		if(predSize==0)
		{
			//the transition will never fire
			fsa->removeState(&(*newState));
			return NULL;
		}
	}
	else
	{
		//add the transition
		fsa->AddTrans(predecessorState, newState, alphabet, false, NULL);		
		link = true;
	}
	
	EncapStIterator_t failState = fsa->AddState(NULL);//create a fail state
	
	//create the eventual extended transition from the previous state
	if(predSize!=0)
	{
		for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator it = currentPredicates.begin(); it != currentPredicates.end(); it++)
		{
			EncapFSA::Alphabet_t extAlphabet = CreateAlphabet(previousProtos,(*it).first,completeAlphabet);
			cout << "ExtAlph: " << extAlphabet.size() << endl;
			if(extAlphabet.size()!=0)
			{
				link = true;
				for(EncapFSA::Alphabet_t::iterator aIt = extAlphabet.begin(); aIt != extAlphabet.end(); aIt++)
				{	
					//this predicate is related to a protocol field, to a counter or both?
					PFLTermExpression *currentPEx = (*it).second;
					if(!currentPEx->MandatoryTunnels())
						//predicate on a protocol field
						fsa->addExtended(predecessorState, *aIt, (inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,currentPEx);
					else				
					{
						//predicate on a counter and eventually on a protocol field
						//note that the counter is surely related to the tunneled, since the header indexing and a repeat operator cannot be specified on the same protocol
						SymbolProto *p = *(((*it).first).begin());
						uint32 value = 2;
						RangeOperator_t op = GREAT_EQUAL_THAN;
						EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, p);//label representing all the symbols leading to proto
						string code = fsa->GetCode2(label);
						code.resize(code.size()-2);
						fsa->addExtended(predecessorState, *aIt,(inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,value,op,code,(currentPEx->GetIRExpr())?currentPEx:NULL); 
					}
				} //end iteration over the alphabet
			}
		}

		if(!link)
		{
			fsa->removeState(&(*newState));
			fsa->removeState(&(*failState));
			return NULL;
		}
	}
	
	/*******SELF LOOP*******/
	
	//create the alphabet for the self loop
	set<SymbolProto*> aux;
	aux.insert(currentProtos.begin(),currentProtos.end());
	for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator it = currentPredicates.begin(); it != currentPredicates.end(); it++)
		aux.insert((*it).first.begin(),(*it).first.end());
	
	//normal transition
	EncapFSA::Alphabet_t alphabetSL = CreateAlphabet(aux,currentProtos,completeAlphabet);
	//EncapFSA::Alphabet_t alphabetSL = CreateAlphabet(currentProtos,currentProtos,completeAlphabet);
	if(alphabetSL.size() != 0)
		//add the self loop
		fsa->AddTrans(newState, newState, alphabetSL, false, NULL);		
		
	//extended transition
	if(predSize!=0)
	{
		for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator it = currentPredicates.begin(); it != currentPredicates.end(); it++)
		{	
			EncapFSA::Alphabet_t alphabetSLExt = CreateAlphabet(aux,(*it).first,completeAlphabet);	
			for(EncapFSA::Alphabet_t::iterator aIt = alphabetSLExt.begin(); aIt != alphabetSLExt.end(); aIt++)
			{
				//this predicate is related to a protocol field, to a counter or both?
				PFLTermExpression *currentPEx = (*it).second;
				if(!currentPEx->MandatoryTunnels())
					//predicate on a protocol field
					fsa->addExtended(newState, *aIt,(inclusionOp!=NOTIN)? newState : failState,(inclusionOp!=NOTIN)? failState : newState,currentPEx); 
				else				
				{
					//predicate on a counter and eventually on a protocol field
					//note that the counter is surely related to the tunneled, since the header indexing and a repeat operator cannot be specified on the same protocol
					SymbolProto *p = *(((*it).first).begin());
					uint32 value = 2;
					RangeOperator_t op = GREAT_EQUAL_THAN;
					EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, p);//label representing all the symbols leading to proto
					string code = fsa->GetCode2(label);
					code.resize(code.size()-2);
					fsa->addExtended(newState, *aIt,(inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,value,op,code,(currentPEx->GetIRExpr())?currentPEx:NULL); 
				}
			}
		}
	}
	
	if(predSize==0)
		fsa->removeState(&(*failState));
	return &(*newState);
}

//manage the repeat operator *
EncapFSA::State *EncapFSABuilder::ManageSTAR(EncapFSA *fsa,set<SymbolProto*>previousProtos,set<SymbolProto*>currentProtos,EncapFSA::State *previousState,PFLRepeatOperator repeatOp,EncapFSA::Alphabet_t completeAlphabet,set<pair<set<SymbolProto*>,PFLTermExpression*> > currentPredicates,PFLInclusionOperator inclusionOp,int predSize)
{
	EncapStIterator_t newState = fsa->AddState(NULL);
	EncapStIterator_t predecessorState = fsa->GetStateIterator(*previousState);

	//add the epsilon transition
	fsa->AddEpsilon(predecessorState, newState, NULL);
	
	//the situation on the self loop is a bit complicated
	//we consider the self loop only for the begin of the transition
	//consider "not ip* in ip". previous is ip, current each protocol but ip. however, as starting symbol of the self loop, we can have ip
	previousProtos.insert(currentProtos.begin(),currentProtos.end());
	EncapFSA::Alphabet_t alphabet = CreateAlphabet(previousProtos,currentProtos,completeAlphabet);
	if(alphabet.size() != 0)
		//add the self loop
		fsa->AddTrans(newState, newState, alphabet, false, NULL);		
		
	//consider now the extended transitions
	if(predSize!=0)
	{
		EncapStIterator_t failState = fsa->AddState(NULL);//create a fail state
		set<SymbolProto*>aux;
		aux.insert(previousProtos.begin(),previousProtos.end());
		for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator it = currentPredicates.begin(); it != currentPredicates.end(); it++)
		{
			aux.insert((*it).first.begin(),(*it).first.end());
		}
	
		for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator it = currentPredicates.begin(); it != currentPredicates.end(); it++)
		{	
			EncapFSA::Alphabet_t alphabetSLExt = CreateAlphabet(aux,(*it).first,completeAlphabet);
			PFLTermExpression *currentPEx = (*it).second;	
			for(EncapFSA::Alphabet_t::iterator aIt = alphabetSLExt.begin(); aIt != alphabetSLExt.end(); aIt++)
			{
				if(!currentPEx->MandatoryTunnels())
					//predicate on a protocol field
					fsa->addExtended(newState, *aIt,(inclusionOp!=NOTIN)? newState : failState,(inclusionOp!=NOTIN)? failState : newState,currentPEx);
				else				
				{
					//predicate on a counter and eventually on a protocol field
					//note that the counter is surely related to the tunneled, since the header indexing and a repeat operator cannot be specified on the same protocol
					SymbolProto *p = *(((*it).first).begin());
					uint32 value = 2;
					RangeOperator_t op = GREAT_EQUAL_THAN;
					EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, p);//label representing all the symbols leading to proto
					string code = fsa->GetCode2(label);
					code.resize(code.size()-2);
					fsa->addExtended(newState, *aIt,(inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,value,op,code,(currentPEx->GetIRExpr())?currentPEx:NULL); 
				}				
			} 
		}
	}
	
	return &(*newState);	

}

//manage the repeat operator ?
EncapFSA::State *EncapFSABuilder::ManageQUESTION(EncapFSA *fsa,set<SymbolProto*>previousProtos,set<SymbolProto*>currentProtos,EncapFSA::State *previousState,PFLRepeatOperator repeatOp,EncapFSA::Alphabet_t completeAlphabet,set<pair<set<SymbolProto*>,PFLTermExpression*> > currentPredicates,PFLInclusionOperator inclusionOp,int predSize)
{

	EncapStIterator_t newState = fsa->AddState(NULL);
	EncapStIterator_t predecessorState = fsa->GetStateIterator(*previousState);

	//add the epsilon transition
	fsa->AddEpsilon(predecessorState, newState, NULL);
	//create the alphabet
	EncapFSA::Alphabet_t alphabet = CreateAlphabet(previousProtos,currentProtos,completeAlphabet);
	if(alphabet.size()!=0)
		//add the transition
		fsa->AddTrans(predecessorState, newState, alphabet, false, NULL);						
	
	if(predSize!=0)
	{
		EncapStIterator_t failState = fsa->AddState(NULL);//create a fail state
	
		for(set<pair<set<SymbolProto*>,PFLTermExpression*> >::iterator it = currentPredicates.begin(); it != currentPredicates.end(); it++)
		{
			nbASSERT(((*it).first).size()==1,"There is a bug. We use a set only for convenience, but there is only one protocol");
			PFLTermExpression *currentPEx = (*it).second;
			EncapFSA::Alphabet_t extAlphabet = CreateAlphabet(previousProtos,(*it).first,completeAlphabet);
			if(extAlphabet.size()!=0)
			{
				for(EncapFSA::Alphabet_t::iterator aIt = extAlphabet.begin(); aIt != extAlphabet.end(); aIt++)
				{
					if(!currentPEx->MandatoryTunnels())
						//predicate on a protocol field
						fsa->addExtended(predecessorState, *aIt, (inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,currentPEx);
					else				
					{
						//predicate on a counter and eventually on a protocol field
						//note that the counter is surely related to the tunneled, since the header indexing and a repeat operator cannot be specified on the same protocol
						SymbolProto *p = *(((*it).first).begin());
						uint32 value = 2;
						RangeOperator_t op = GREAT_EQUAL_THAN;
						EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(NULL, p);//label representing all the symbols leading to proto
						string code = fsa->GetCode2(label);
						code.resize(code.size()-2);
						fsa->addExtended(predecessorState, *aIt,(inclusionOp!=NOTIN)? newState : failState, (inclusionOp!=NOTIN)? failState : newState,value,op,code,(currentPEx->GetIRExpr())?currentPEx:NULL); 
					}
				}
			}//end (extAlphabet.size()!=0)
		}//end iteration over the predicates
	}
	
	return &(*newState);	
}

//create an alphabet
EncapFSA::Alphabet_t EncapFSABuilder::CreateAlphabet(set<SymbolProto*>previousProtos,set<SymbolProto*>currentProtos,EncapFSA::Alphabet_t completeAlphabet)
{
	EncapFSA::Alphabet_t alphabet;
	for(set<SymbolProto*>::iterator p = previousProtos.begin(); p != previousProtos.end();p++)
	{
		for(set<SymbolProto*>::iterator c = currentProtos.begin(); c != currentProtos.end();c++)	
		{
			EncapLabel_t label = std::make_pair<SymbolProto*, SymbolProto*>(*p, *c);
			EncapFSA::Alphabet_t::iterator l = completeAlphabet.begin();
			for (; l != completeAlphabet.end(); l++)
			{
				if(*l == label)
				{
					alphabet.insert(label);
					break;
				}
			}		
		}
	}
	return alphabet;
}

//returns an iterator of a state
EncapFSA::StateIterator EncapFSA::GetStateIterator(EncapFSA::State &state)
{
	EncapFSA::StateIterator s = this->FirstState();
	while(s != this->LastState())
	{
		if((*s).GetID() == state.GetID())
			break;
		s++;
	}
	return s;
}

//returns true if the header chain begins with startproto
bool EncapFSABuilder::StartAsFirst(list<PFLSetExpression*>* innerList)
{
	list<PFLSetExpression*>::reverse_iterator it = innerList->rbegin();
	if((*it)->IsAnyPlaceholder())
		return false;
	else
	{
		list<PFLTermExpression*> *elements = (*it)->GetElements();
		list<PFLTermExpression*>::iterator element = elements->begin();	
		string protoName = (*element)->GetProtocol()->Name;
		if(protoName == "startproto")
			return true;
		return false;
	}
}

//algorithm assign/prune/expand/prune
void EncapFSABuilder::FixAutomaton(EncapFSA *fsa)
{
	std::list<EncapFSA::State*> accepting = fsa->GetAcceptingStates();
	if(accepting.size()==0)
		//the automaton has not the final accepting state
		return CreateNoMatchAutomaton(fsa);
		
	//create final not accepting state and add the self loop on it
	EncapStIterator_t finalNotAccept = fsa->AddState(NULL);
	fsa->resetAccepting(finalNotAccept);
	fsa->setFinal(finalNotAccept);
	Alphabet_t empty;
	fsa->AddTrans(finalNotAccept, finalNotAccept, empty, true, NULL);
	EncapFSA::State *fail = &(*finalNotAccept);

	/***IMPORTANT
	*
	* the final accepting state, the final not accepting state and the related self loops
	* cannot be removed during the algrithm
	*
	***/

	if(!AssignProtocol(fsa,fail))
	{
		PRINT_DOT(fsa,"fixautomaton - after the assignment failed","regexpfsa_after_assignment_failed");
		return CreateNoMatchAutomaton(fsa); 
	}
	PRINT_DOT(fsa,"fixautomaton - after the assignment","regexpfsa_after_assignment");
	
	if(!ExpandStates(fsa,fail))
	{
		PRINT_DOT(fsa,"fixautomaton - after the expansion failed","regexpfsa_after_expansion_failed");
		return CreateNoMatchAutomaton(fsa);
	}
	PRINT_DOT(fsa,"fixautomaton - after the expansion","regexpfsa_after_expansion");
	
	FindTraps(fsa,fail);
	
	PRINT_DOT(fsa,"fixautomaton - after the remotion of the traps","regexpfsa_after_traps_remotion");
	
	LinkToNotAccept(fsa,fail);
	
	EncapStIterator_t finalAccept = fsa->GetStateIterator(*(accepting.begin()));
	fsa->setFinal(finalAccept);

	PRINT_DOT(fsa,"fixautomaton - end","regexpfsa_end_fixautomaton");

}

bool EncapFSABuilder::AssignProtocol(EncapFSA *fsa, EncapFSA::State *fail)
{
	bool change = true;
	bool ok = true;
	
	//repeat untill there are changes in the automaton
	while(change)
	{	
		change = false;
		//iterate over the states of teh automaton
		EncapFSA::StateIterator s = fsa->FirstState();
  		for (; s != fsa->LastState(); ++s)
  		{
  			if((*s).GetInfo() != NULL)
  				//the current state is already related to a protocol
  				continue;
  				
  			if((*s).isFinal() and !(*s).isAccepting())
  				//the current state is the final not accepting state, and we do not consider it
  				continue;
  			
  			//if the current state is the starting state of the automaton, and it is without entering transitions
  			//assign this state to startproto
  			if((*s).IsInitial())
  			{
  				if(fsa->getTransEnteringTo(&(*s)).size()==0)
  				{
	  				change = true;
	  				EncapGraph::GraphNode &startNode = m_Graph.GetStartProtoNode();
	  				SymbolProto *startProto = startNode.NodeInfo;
	  				(*s).SetInfo(startProto);
	  				CutOutgoingTransitions(fsa,&(*s));//remove the symbols on the outgoing transitions and beginning with a protocol different than startproto
	  				ok = RemoveUselessStates(fsa,fail); //remove the states useless for the automaton
					if(!ok)
						//the regular expression cannot be mathced
						return false;
				}
	  			continue;
  			}
  			
  			//iterate over the incoming transitions of the current state
  			//store the destination protocols of the symbols into a set
  			//after the iteration, if the set has only one element, assign that protocol to the state
  			set<SymbolProto*> incomingProtos;
  			list<EncapFSA::Transition*> transition = fsa->getTransEnteringTo(&(*s));
			for(list<EncapFSA::Transition*>::iterator t = transition.begin(); t != transition.end(); t++)
			{
				Alphabet_t labels = (*t)->GetInfo().first;
				for (Alphabet_t::iterator l = labels.begin();l != labels.end();++l)
      			{
      				SymbolProto *proto = (*l).second;
      				incomingProtos.insert(proto);
      			}//end iteration over the labels
			}//end iteration over the transitions
			if(incomingProtos.size()==1)
			{
				//this state can be assigned to a single protocol
				change = true;
				(*s).SetInfo(*(incomingProtos.begin()));
				if((*s).isAccepting())
					continue; //we do not remove the self loop on the final accepting state
				CutOutgoingTransitions(fsa,&(*s));//remove the symbols on the outgoing transitions and beginning with a protocol different than the one assigned to the state
				ok = RemoveUselessStates(fsa,fail); //remove the states useless for the automaton
				if(!ok)
					//the regular expression cannot be mathced
					return false;
			}
  		}//end iteration over the states			
  		PRINT_DOT(fsa,"fixautomaton - after an assignment loop","regexpfsa_assignment_loop");

	}
	
	return true;
}

//remove the symbols (and possibly the transitions) starting with a protocol different from the one related to the
//starting state of the transition
//if this state has an outgoing extended transition, also the extended transition must be removed
void EncapFSABuilder::CutOutgoingTransitions(EncapFSA *fsa, EncapFSA::State *state)
{
	SymbolProto *stateProto = state->GetInfo();
	
	set<EncapFSA::ExtendedTransition*> etToRem;
	
	list<EncapFSA::Transition*> transition = fsa->getTransExitingFrom(state);
	for(list<EncapFSA::Transition*>::iterator t = transition.begin(); t != transition.end();)
	{
		list<EncapFSA::Transition*>::iterator aux = t;//needed in case of the transition must be removed
		Alphabet_t labels = (*t)->GetInfo().first;
		//iterate over the labels of the transition  
		for (Alphabet_t::iterator l = labels.begin();l != labels.end();++l)
      	{
      		SymbolProto *proto = (*l).first;
      		if(stateProto!=proto)
      		{
      			//we have to remove the label from the transition
      			(*t)->RemoveLabel(*l);
      		}
      	}//end iteration over the labels
      	t++;
      	if(((*aux)->GetInfo().first).size()==0)
      	{
      		//we have to remove the transition
      		
      		//store the eventual related extended transition
      		if((*aux)->getIncludingET())
      			etToRem.insert((*aux)->getIncludingET());
      		(*aux)->RemoveEdge();
			delete(*aux);
      	}
	}//end iteration over the transitions
	
	//remove the extended transitions, if needed
	for(set<EncapFSA::ExtendedTransition*>::iterator it = etToRem.begin(); it != etToRem.end();)
	{
		set<EncapFSA::ExtendedTransition*>::iterator aux = it;
		it++;
		delete(*aux);	
	}
}

//remove the states without exiting or entering transitions, obviously except the self loop.
//obviously, the initial state is removed only in case of it has not exiting transitions
//while the final (both accepting and not accepting) states is removed only if it has not entering transitions
//if a state has an incoming extended transition, we redirect the extended transition toward the final not accepting state
bool EncapFSABuilder::RemoveUselessStates(EncapFSA *fsa, EncapFSA::State *fail)
{
	bool change = true;
	bool ok = true;
	while(change)
	{
		change = false;
		//iterate over the states of teh automaton
		EncapFSA::StateIterator s = fsa->FirstState();
  		for (; s != fsa->LastState();)
  		{
  			if((*s).isFinal() and !(*s).isAccepting())
  			{
  				//this state is the final not accepting state
  				s++;
  				continue;
  			}
  			
  			EncapFSA::StateIterator aux = s; //useful in case of the current state must be removed
  			s++;
			if(!(*aux).IsInitial())
			{
				list<EncapFSA::Transition*> transition = fsa->getTransEnteringTo(&(*aux));
				if(transition.size()==0 or (transition.size()==1 and ThereIsSelfLoop(transition)))
				{
					//the state has not incoming tranistions, then it must be removed
					change = true;
					
					//first of all, we must remove the outgoing transitions (both normal than extended)
					CutOutgoingTransitions(fsa, &(*aux));
					
					if((*aux).isAccepting())
						//the regular expression will be never mathced, since the final accepting state has been removed
						ok = false;
					fsa->removeState(&(*aux));
					continue;
				}
			}
			if(!(*aux).isAccepting())
			{
				list<EncapFSA::Transition*> transition = fsa->getTransExitingFrom(&(*aux));
				if(transition.size()==0 or (transition.size()==1 and ThereIsSelfLoop(transition)))
				{
					
					change = true;
					
					//first of all, we must remove the normal incoming transitions. the extended must be redirect toward the final not accepting state
					CutIncomingTransitions(fsa,&(*aux),fail);					
					
					//remove the state
					fsa->removeState(&(*aux));
					continue;
				}//end (transition.size()==0)
			}
		}//end iteration over the transitions 
	}
	
	return ok;
}

//given a state, removes its incoming transitions. In case of an extended transition, it is redirected to the final not accepting state
void EncapFSABuilder::CutIncomingTransitions(EncapFSA *fsa, EncapFSA::State *state, EncapFSA::State *fail)
{
	//renove the incoming transitions, except the extended which are redirected to the final not accepting state
	list<EncapFSA::Transition*> transitionIn = fsa->getTransEnteringTo(state);
	for(list<EncapFSA::Transition*>::iterator it = transitionIn.begin(); it != transitionIn.end();)
	{
		list<EncapFSA::Transition*>::iterator auxTr = it;
		it++;
		
		EncapFSA::ExtendedTransition *et = (*auxTr)->getIncludingET();
		if(et)
		{				
			//we redirect the extended transition toward the final not accepting state
			map<EncapFSA::State*,EncapFSA::State*> ms;

			ms[&(*state)] = fail;
			et->changeGateRefs(NULL,NULL,ms);

			Alphabet_t alph = (*auxTr)->GetLabels();

			EncapStIterator_t failIt = fsa->GetStateIterator(fail);

			EncapStIterator_t fromIt = (*auxTr)->FromState();

			EncapTrIterator_t newTransIt = fsa->AddTrans(fromIt, failIt, alph, false, NULL);
			EncapTrIterator_t oldTransIt = fsa->GetTransIterator(*auxTr);
			(*newTransIt).setIncludingET(et);
			et->changeEdgeRef(oldTransIt, newTransIt);
		}
		else
		{
			//this transition is not related to an extended transition, hence we simply remove it
			(*auxTr)->RemoveEdge();
			delete(*auxTr);
		}
	}//end iteration over the incoming transitions
}

//return true if there is a self loop
bool EncapFSABuilder::ThereIsSelfLoop(list<EncapFSA::Transition*> transition)
{
	for(list<EncapFSA::Transition*>::iterator it = transition.begin(); it != transition.end(); it++)
	{
		if((*it)->FromState() == (*it)->ToState())
			return true;
	}
	return false;
}

//expand a state without protocol in a new state for each destination protocol of the labels on its incoming transitions
bool EncapFSABuilder::ExpandStates(EncapFSA *fsa,EncapFSA::State *fail)
{
	//iterate over the states of teh automaton
	EncapFSA::StateIterator s = fsa->FirstState();
	for (; s != fsa->LastState();)
	{
		if((*s).GetInfo() != NULL)
		{
			//the current state is already related to a protocol
			s++;
			continue;
		}
		
		if((*s).isFinal() and !(*s).isAccepting())
		{
			//the current state is the final not accepting state
			s++;
			continue;
		}
		
		//the current state must be removed and replaced by a new state for each second protocol of the labels on the entering transitions
		
		map<SymbolProto*,EncapFSA::State*> newStates; 					//new states replacing the current one
		Alphabet_t onSelfLoop;						  					//symbols on self loop
		map<int,map<EncapLabel_t,EncapFSA::State*> > labelFromState; 	//symbols on incoming transitions, and related starting state
		map<EncapLabel_t,EncapFSA::State*> labelToState; 				//symbols on outgoing transitions, and related destination state
				
		/************INCOMING*********************/

		//iterate over the incoming transitions, including the eventual self loop. 
		//For each symbol, store the destination state. remove the transition.
		//if the transition is part of an extended transition, simply change the destination state with the right new state
		//for each new destination protocol, create a new state
		
		list<EncapFSA::Transition*> transitionIn = fsa->getTransEnteringTo(&(*s));
		int counter = 0;//it is needed since different entering transitions could have the same label
		for(list<EncapFSA::Transition*>::iterator t = transitionIn.begin(); t != transitionIn.end();)		
		{
			bool selfLoop = ((*t)->ToState() == (*t)->FromState())? true : false;	
			
			Alphabet_t labels = (*t)->GetInfo().first;
			list<EncapFSA::Transition*>::iterator aux = t;
			t++;
			if(!(*aux)->getIncludingET())
			{
				map<EncapLabel_t,EncapFSA::State*> currentLabelFromState;
				for (Alphabet_t::iterator l = labels.begin();l != labels.end();++l)
				{
					if(selfLoop)
						//this is the self loop
						onSelfLoop.insert(*l);
					else
						currentLabelFromState.insert(pair<EncapLabel_t,EncapFSA::State*>(*l,&(*(*aux)->FromState())));
				
					CreateNewStateIfNeeded(fsa,&newStates,*l);

	  			}//end iteration over the alphabet		

				labelFromState.insert(pair<int,map<EncapLabel_t,EncapFSA::State*> >(counter,currentLabelFromState));
				counter++;
			}
			else
			{
				//this incoming transition is part of an extended transition
				//we redirect the extended transition toward the right new state
				nbASSERT(labels.size()==1,"An extended transition with more than one label?");
				EncapLabel_t label = *labels.begin();
				CreateNewStateIfNeeded(fsa,&newStates,label);
				
				map<EncapFSA::State*,EncapFSA::State*> ms;

				map<SymbolProto*,EncapFSA::State*>::iterator newTo = newStates.find(label.second);
				EncapFSA::State *newState = (*newTo).second; //new destination state of the transition

				EncapStIterator_t oldState = (*aux)->ToState(); //old destination state of the transition
				
				ms[&(*oldState)] = newState;
				
				EncapFSA::ExtendedTransition *et = (*aux)->getIncludingET();
				et->changeGateRefs(NULL,NULL,ms);

				EncapStIterator_t newToIt = fsa->GetStateIterator(newState); //iterator of the new destination state of the transition
				EncapStIterator_t fromState = (*aux)->FromState();	//starting state of the transitions (old and new)
				EncapTrIterator_t newTransIt = fsa->AddTrans(fromState, newToIt, labels, false, NULL);
				EncapTrIterator_t oldTransIt = fsa->GetTransIterator(*aux);
				(*newTransIt).setIncludingET(et); //set the new transition as part of the extended transition
				et->changeEdgeRef(oldTransIt, newTransIt);
			}
			//remove the transition
			(*aux)->RemoveEdge();
			delete(*aux);
  		
      	}//end iteration over the incoming transitions
      	
      	//since this state is the initial state of the automaton, it must be replaced also by the a state related to startproto, which must
     	//be set as new initial state
      	if((*s).IsInitial())
      	{
      		EncapGraph::GraphNode &startNode = m_Graph.GetStartProtoNode();
  			SymbolProto *startProto = startNode.NodeInfo;
  			EncapStIterator_t state = this->AddState(*fsa, startNode, true);
  			newStates.insert(pair<SymbolProto*,EncapFSA::State*>(startProto,&(*state)));
  			fsa->SetInitialState(state);
      	}

		/************OUTGOING*********************/
		
		//iterate over the outgoing transitions, including the eventual self loop. 
		//For each symbol, store the starting state. remove the transition.
		//if the transition is part of an extended transition, simply change the starting state with the right new state
		
		set<EncapFSA::ExtendedTransition*> etToRemove;
		list<EncapFSA::Transition*> transitionOut = fsa->getTransExitingFrom(&(*s));
		for(list<EncapFSA::Transition*>::iterator t = transitionOut.begin(); t != transitionOut.end();)		
		{
			Alphabet_t labels = (*t)->GetInfo().first;
			if((*t)->getIncludingET())
			{
				//this outgoing transition is part of an extended transition
				//we change the starting state of the extended transition
				nbASSERT(labels.size()==1,"An extended transition with more than one label?");
				EncapLabel_t label = *labels.begin();
								
				EncapFSA::ExtendedTransition *et = (*t)->getIncludingET();
								
				map<SymbolProto*,EncapFSA::State*>::iterator newFrom = newStates.find(label.first); //new starting state of the transition
				
				if(newFrom != newStates.end())
				{
					//a new state for the starting protocol of the transition exists
					EncapFSA::State *newState = (*newFrom).second;
				
					map<EncapFSA::State*,EncapFSA::State*> ms;//needed to compile the c++ code :D	
					et->changeGateRefs(NULL,newState,ms);
				
					EncapStIterator_t destination = (*t)->ToState();
					EncapStIterator_t source = fsa->GetStateIterator(newState);
					EncapTrIterator_t newTransIt = fsa->AddTrans(source, destination, label, false, NULL);
					(*newTransIt).setIncludingET(et);
				
					EncapTrIterator_t oldTransIt = fsa->GetTransIterator(*t);
				
					et->changeEdgeRef(oldTransIt, newTransIt);
				}
				else
					//there is not a new starting state for this extended transition, then it must be removed
					etToRemove.insert(et);
			}
			else
			{		
				//iterate over the labels, and store each label in labelToState with the destination state of the transition
				for (Alphabet_t::iterator l = labels.begin();l != labels.end();++l)
					labelToState.insert(pair<EncapLabel_t,EncapFSA::State*>(*l,&(*(*t)->ToState())));
			}

			//remove the transition
			list<EncapFSA::Transition*>::iterator aux = t;
			t++;
			(*aux)->RemoveEdge();
			delete(*aux);
		
		}//end iteration over the outgoing transitions
		
		//remove the useless exiting extended transitions
		for(set<EncapFSA::ExtendedTransition*>::iterator etIt = etToRemove.begin(); etIt != etToRemove.end(); )
		{
			set<EncapFSA::ExtendedTransition*>::iterator auxEt = etIt;
			etIt++;
			delete(*auxEt);
		}	
		
		
      	//the current state has been expanded
     	//now we have to put the transitions of the old state on the right new state
     	ReplaceTransitions(fsa,labelToState,labelFromState,onSelfLoop,newStates);
      	
      	//remove the state
     	EncapFSA::StateIterator sAux = s;
     	s++;
     	fsa->removeState(&(*sAux));
     	
     	PRINT_DOT(fsa, "fixautomaton - after an expansion loop", "regexpfsa_expansion_loop");
	}//end iteration over the states
	return RemoveUselessStates(fsa,fail);
}

//Create a new state for the second protocol of the label, if it is not exist yet
void EncapFSABuilder::CreateNewStateIfNeeded(EncapFSA *fsa, map<SymbolProto*,EncapFSA::State*> *newStates, EncapLabel_t label)
{
	if(newStates->count(label.second)==0)
 	{
		//a new state related to this protocol does not exist
		EncapGraph::GraphNode *node = m_Graph.GetNode(label.second);
		EncapStIterator_t state = this->AddState(*fsa, *node, true);
		newStates->insert(pair<SymbolProto*,EncapFSA::State*>(label.second,&(*state)));
  	}
}	

//link the new states to the rest of the automaton, using the incoming and outgoing transitions of the state that has been replaced
void EncapFSABuilder::ReplaceTransitions(EncapFSA *fsa,map<EncapLabel_t,EncapFSA::State*> labelToState,map<int,map<EncapLabel_t,EncapFSA::State*> > labelFromState,Alphabet_t onSelfLoop,map<SymbolProto*,EncapFSA::State*> newStates)
{
	//replace the selfloop - except the extended transitions
	for (Alphabet_t::iterator l = onSelfLoop.begin();l != onSelfLoop.end();++l)
	{
		SymbolProto *fromProto = (*l).first;
		SymbolProto *toProto = (*l).second;
		map<SymbolProto*,EncapFSA::State*>::iterator from = newStates.find(fromProto);
		map<SymbolProto*,EncapFSA::State*>::iterator to = newStates.find(toProto);
		if(from != newStates.end() and to != newStates.end())
		{
			//the transition must me put in the automaton
			EncapStIterator_t fromState = fsa->GetStateIterator((*from).second);
			EncapStIterator_t toState = fsa->GetStateIterator((*to).second);
			fsa->AddTrans(fromState, toState, *l, false, NULL);
		}
	}
	
	//replace the incoming transitions - except the extended transitions
	map<int,map<EncapLabel_t,EncapFSA::State*> >::iterator superIt = labelFromState.begin();
	for(; superIt != labelFromState.end(); superIt++)
	{
		map<EncapLabel_t,EncapFSA::State*> currentLabelFromState = (*superIt).second;
	
		map<EncapLabel_t,EncapFSA::State*>::iterator fr = currentLabelFromState.begin();
		for(; fr != currentLabelFromState.end(); fr++)
		{
			EncapLabel_t label = (*fr).first;
			map<SymbolProto*,EncapFSA::State*>::iterator to = newStates.find(label.second);
			if(to != newStates.end())
			{
				//the transition must be put on the automaton
				EncapStIterator_t fromState = fsa->GetStateIterator((*fr).second);
				EncapStIterator_t toState = fsa->GetStateIterator((*to).second);
				fsa->AddTrans(fromState, toState, label, false, NULL);
			}
		}
	}
	
	//replace the outgoing transitions - except the extended transitions
	map<EncapLabel_t,EncapFSA::State*>::iterator to = labelToState.begin();
	for(; to != labelToState.end(); to++)
	{
		EncapLabel_t label = (*to).first;
		map<SymbolProto*,EncapFSA::State*>::iterator from = newStates.find(label.first);
		if(from != newStates.end())
		{
			//the transition must be put on the automaton
			EncapStIterator_t fromState = fsa->GetStateIterator((*from).second);
			EncapStIterator_t toState = fsa->GetStateIterator((*to).second);
			fsa->AddTrans(fromState, toState, label, false, NULL);
		}
	}
	
}

bool EncapFSABuilder::FindTraps(EncapFSA *fsa,EncapFSA::State *fail)
{
	bool again = false; //if at the end of the method this variable is true, the method is executed again

	//perform a sort reverse postorder visit of the automaton, in order to mark the useful states
	list<EncapFSA::State*> stateList = fsa->SortRevPostorder();
	
	//iterate over the states and remove te ones not marked
	EncapFSA::StateIterator s = fsa->FirstState();
	for (; s != fsa->LastState();)
	{
		if((*s).HasBeenVisited())// or HasIncomingExtendedTransition(fsa,&(*s)))
		{
			//this state has been visited
			s++;
			continue;
		}
		
		if((*s).isFinal() and !(*s).isAccepting())
		{
			//this is the final not accepting state
			s++;
			continue;
		}
			
		again = true;
			
		//the current state must be removed
		
		EncapFSA::StateIterator aux = s;
		s++;
		
		//remove the outgoing transitions of the state
		list<EncapFSA::Transition*> transitionOut = fsa->getTransExitingFrom(&(*aux));
		CutOutgoingTransitions(fsa, &(*aux));
		
		//remove the incoming transitions, except the extended which are redirected to the final not accepting state
		CutIncomingTransitions(fsa,&(*aux),fail);
		
		//remove the state
		fsa->removeState(&(*aux));
	}

	RemoveUselessStates(fsa,fail);
	
	return (again)? FindTraps(fsa,fail) : true;

}

void EncapFSABuilder::LinkToNotAccept(EncapFSA *fsa,EncapFSA::State *fail)
{
	
	EncapStIterator_t finalNotAccept = fsa->GetStateIterator(fail);
	
	//iterate over the states of the automaton
	EncapFSA::StateIterator s = fsa->FirstState();
	for (; s != fsa->LastState();s++)
	{
		if((*s).isFinal() or (*s).isAccepting())
			//the final accepting state and the final not accepting state have not the transition toward the final not accepting state
			continue;
			
		Alphabet_t usedLabels;
		//iterate over the outgoing transitions of the current state
		list<EncapFSA::Transition*> transitionOut = fsa->getTransExitingFrom(&(*s));
		for(list<EncapFSA::Transition*>::iterator t = transitionOut.begin(); t != transitionOut.end();t++)		
		{
			Alphabet_t labels = (*t)->GetInfo().first;
			for (Alphabet_t::iterator l = labels.begin();l != labels.end();++l)
				usedLabels.insert(*l);	
		} //end iteration over the outgoing transitions
		
		//link the current state and the final not accepting state
		EncapStIterator_t state = fsa->GetStateIterator(*s);
		fsa->AddTrans(state, finalNotAccept, usedLabels, true, NULL);	
		
	}//end iteration over the states
}

void EncapFSABuilder::CreateNoMatchAutomaton(EncapFSA *fsa)
{
	//remove each transition and each state of the automaton, except the initial state
	EncapFSA::StateIterator s = fsa->FirstState();
	for (; s != fsa->LastState();)
	{			
		//remove the outgoing transitions of the state	
		CutOutgoingTransitions(fsa, &(*s));
			
		//remove the state
		EncapFSA::StateIterator sAux = s;
		s++;
		if(!(*sAux).IsInitial())
			fsa->removeState(&(*sAux));
	}//end iteration over the states
	
	EncapFSA::StateIterator sIt = fsa->FirstState();
	for (; sIt != fsa->LastState();sIt++)
	{
		//the automaton has now only one state
		if((*sIt).GetInfo() == NULL)
		{
			EncapGraph::GraphNode &startNode = m_Graph.GetStartProtoNode();
  			SymbolProto *startProto = startNode.NodeInfo;
  			(*sIt).SetInfo(startProto);
		}

		//create final not accepting state
		EncapStIterator_t finalNotAccept = fsa->AddState(NULL);
		fsa->resetAccepting(finalNotAccept);
		fsa->setFinal(finalNotAccept);
		//add the self loop on the final not accepting state
		Alphabet_t empty;
		fsa->AddTrans(finalNotAccept, finalNotAccept, empty, true, NULL);
				
		//link the initial state and the final not accepting state
		EncapStIterator_t state = fsa->GetStateIterator(*sIt);
		fsa->AddTrans(state, finalNotAccept, empty, true, NULL); 
		
		break;
	}
	
	PRINT_DOT(fsa,"fixautomaton - end","regexpfsa_without_accept_fixautomaton");
}
