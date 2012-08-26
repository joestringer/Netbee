/*
 * DFAset.h
 *
 *  Created on: 27-lug-2009
 *      Author: Nik
 */

#include "encapfsa.h"

class DFAset
{

private:
	EncapFSA::Alphabet_t symbols;
	SymbolProto *info;
	int id;
        static int next_id;
	std::map<uint32,EncapFSA::State*> states;

        /* The following variable aims at obtaining one useful
         * optimization: if 'states' includes at least one accepting
         * final state, store it in this variable, as the whole DFAset
         * can be compacted into it (and just return it if asked by
         * the outside world).
         */
        EncapFSA::State* states_acceptingfinal;

public:

 DFAset() : states()
	{
		this->info = NULL;
		this->id = next_id++;
                this->states_acceptingfinal = NULL;
	}

	DFAset(const DFAset &other) : symbols(other.symbols), info(other.info),
          id(other.id), states(other.states),
          states_acceptingfinal(other.states_acceptingfinal) {}

        DFAset(std::set<EncapFSA::State*> stateset)
        {
          this->info = NULL;
          this->id = next_id++;
          this->states_acceptingfinal = NULL; // default to NULL

          for (std::set<EncapFSA::State*>::iterator i = stateset.begin();
               i != stateset.end();
               ++i)
            // AddState will take care of setting 'states_acceptingfinal'
            this->AddState(*i);
        }

        DFAset(DFAset *a, DFAset *b){
          this->info = NULL;
          this->id = next_id++;

          this->states.insert(a->states.begin(), a->states.end());
          this->states.insert(b->states.begin(), b->states.end());

          nbASSERT(a->states_acceptingfinal == NULL ||
                   b->states_acceptingfinal == NULL ||
                   a->states_acceptingfinal == b->states_acceptingfinal,
                   "different DFAsets have different final accepting states?");
          this->states_acceptingfinal = (a->states_acceptingfinal ?
                                         a->states_acceptingfinal :
                                         b->states_acceptingfinal );
        }

	SymbolProto* GetInfo(void)
	{
		return this->info;
	}

	int GetId(void)
	{
		return this->id;
	}

        // Resolve internally what is the stateInfo shared among the states, if any.
        // Return it.
        SymbolProto *AutoSetInfo(void)
        {
          // try and use the information from the states currently inside the set, if consistent
          std::map<uint32,EncapFSA::State*>::iterator s = this->states.begin();
          SymbolProto *tmp_info = NULL;
          for(; s != this->states.end(); ++s) {
            EncapFSA::State *s_ptr = s->second;

            if (s_ptr->GetInfo() == NULL)
              continue; // skip useless states

            if (tmp_info == NULL)
              tmp_info = s_ptr->GetInfo();
            else if (tmp_info != s_ptr->GetInfo())
              return NULL; // conflict found, abort
          }

          this->info = tmp_info;
          return tmp_info;
	}

	std::map<uint32,EncapFSA::State*> GetStates(void)
	{
          if (states_acceptingfinal != NULL) {
            std::map<uint32,EncapFSA::State*> tmp;
            tmp[states_acceptingfinal->GetID()] = states_acceptingfinal;
            return tmp;
          }

          return this->states;
	}

	EncapFSA::Alphabet_t GetSymbols(void)
	{
		return this->symbols;
	}

	void AddState(EncapFSA::State *s)
	{
		std::pair<uint32, EncapFSA::State*> p = make_pair<uint32, EncapFSA::State*>(s->GetID(), s);
		states.insert(p);
                if (states_acceptingfinal == NULL &&
                    s->isAccepting() &&
                    s->isFinal() ){
                  states_acceptingfinal = s;
                  }
	}

	void AddSymbol(EncapLabel_t s)
	{
		if(!symbols.contains(s))
		{
			symbols.insert(s);
		}
	}

        /*
	void parseAcceptingStatus(void)
	{
		std::map<uint32,EncapFSA::State*>::iterator iter = (this->states).begin();
		while(iter != (this->states).end())
		{
			if((*iter).second->IsAccepting())
			{
				this->Accepting = true;
				this->notAccepting = false;
			}
			iter++;
		}
	}
        */

	bool isAccepting(void)
	{
          if(states_acceptingfinal)
            return true;

          std::map<uint32,EncapFSA::State*>::iterator iter = (this->states).begin();
          while(iter != (this->states).end())
            {
              if((*iter).second->isAccepting())
                return true;
              iter++;
            }
          return false;
	}

	bool isAcceptingFinal(void)
	{
          return (states_acceptingfinal != NULL);
	}

        /*
	bool IsNotAccepting(void)
	{
		return this->notAccepting;
	}
        */


	int32 CompareTo(const DFAset &other) const
	{
/*          nbASSERT(this->states_acceptingfinal == NULL ||
                   other.states_acceptingfinal == NULL ||
                   this->states_acceptingfinal == other.states_acceptingfinal,
                   "different DFAsets have different final accepting states?");
*/
          if (this->states_acceptingfinal != NULL && other.states_acceptingfinal != NULL && this->states_acceptingfinal == other.states_acceptingfinal)
            return 0;

		std::map<uint32,EncapFSA::State*> s2 = other.states;
		std::map<uint32,EncapFSA::State*> s1 = this->states;

                for (std::map<uint32,EncapFSA::State*>::iterator is2 = s2.begin();
                     is2 != s2.end();
                     ++is2)
		{
                  // optimization: ignore final-non-accepting states when doing this comparison, as
                  // they bring no difference to those DFAsets
                  if((*is2).second->isFinal() && !(*is2).second->isAccepting())
                    continue;

                  if(s1.find((*is2).first) == s1.end())
                    return -1;
		}

                for(std::map<uint32,EncapFSA::State*>::iterator is1 = s1.begin();
                    is1 != s1.end();
                    ++is1)
		{
                  // optimization: ignore final-non-accepting states when doing this comparison, as
                  // they bring no difference to those DFAsets
                  if((*is1).second->isFinal() && !(*is1).second->isAccepting())
                    continue;

                  if(s2.find((*is1).first) == s2.end())
                    return 1;
		}
		return 0;
	}

};

int DFAset::next_id = 1;
