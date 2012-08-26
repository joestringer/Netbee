/*****************************************************************************/
/*                                                                           */
/* Copyright notice: please read file license.txt in the NetBee root folder. */
/*                                                                           */
/*****************************************************************************/



#ifndef _SFT_WRITER_H
#define _SFT_WRITER_H

#include "sft_defs.h"
#include "sft.hpp"
#include <ostream>
#include <string>


/*!
	\brief This class provides functionalities for dumping on an output stream (e.g. a file)
			the structure of an FSA in Dot format (see http://www.graphviz.org/)
 */

template <class FSAType>
class sftDotWriter
{
	std::ostream &m_Stream;	//!< the output stream
	bool	m_Horiz;	//!< if true the fsa will be layd out horizontally
	uint32	m_TransLblSize;	//!< size in points of a transition label
	uint32	m_StateLblSize;	//!< size in points of a state label
	double  m_StateSize;	//!< size of a state circle


public:


	/*!
		\brief constructor
		\param o_str the output stream (e.g. a file)
		\param horiz flag for the fsa layout (horizontal if true, vertical otherwise)
		\param trLblSz size in points of a transition label
		\param stLblSz size in points of a state label
		\param stSz size of a state circle

	 */
	sftDotWriter(std::ostream &o_str, bool horiz = true, uint32 trLblSz = 10, uint32 stLblSz = 11, double stSz = 0.65)
		:m_Stream(o_str), m_Horiz(horiz), m_TransLblSize(trLblSz), m_StateLblSize(stLblSz), m_StateSize(stSz){}

	/*!
	 * \brief Dump an FSA
	 * \param f the FSA
	 */
	void DumpFSA(FSAType *f);

};


template <class FSAType>
void sftDotWriter<FSAType>::DumpFSA(FSAType *f)
{
	typedef FSAType fsa_t;
        std::set<typename fsa_t::ExtendedTransition*> ET_set;
	std::string dir;
	if (m_Horiz)
		dir = "LR";
	else
		dir = "TB";
	typename fsa_t::StateIterator initial = f->GetInitialState();
	m_Stream << "Digraph fsa{" << std::endl;
	m_Stream << "\trankdir=" << dir << std::endl;
	m_Stream << "\tedge[fontsize=" << m_TransLblSize << "];" << std::endl;
	m_Stream <<	"\tnode[fontsize=" << m_StateLblSize << ", fixedsize=true, shape=circle, ";
	m_Stream << "width=" << m_StateSize << ",heigth=" << m_StateSize << "];" << std::endl;
	m_Stream << "\tstart__[style=invis];" << std::endl;

	for (typename fsa_t::StateIterator i = f->FirstState(); i != f->LastState(); i++)
	{
          m_Stream << "\tst_" << (*i).GetID() << "[label=\"(" << (*i).GetID() <<
                   ")" << (*i).ToString() << "\"";
		if ((*i).isAccepting())
			m_Stream << ",shape=doublecircle";
		m_Stream << "];" << std::endl;
	}
	m_Stream << "\tstart__->" << "st_" << (*initial).GetID() << ";" << std::endl;

	for (typename fsa_t::TransIterator j = f->FirstTrans(); j != f->LastTrans(); j++)
	{
		m_Stream << "\tst_" << (*(*j).FromState()).GetID() << " ->" << "st_" << (*(*j).ToState()).GetID();
		m_Stream << "[label=\"" << (*j).ToString() << "\"";
                if ((*j).getIncludingET()){
                  m_Stream << ",color=crimson,fontcolor=crimson";
                  ET_set.insert((*j).getIncludingET());
                }
                m_Stream << "];" << std::endl;
	}

        for (typename std::set<typename fsa_t::ExtendedTransition*>::iterator et = ET_set.begin();
             et != ET_set.end();
             et++){
          (*et)->dumpToStream(m_Stream);
        }

	m_Stream << "}" << std::endl;

}

#endif
