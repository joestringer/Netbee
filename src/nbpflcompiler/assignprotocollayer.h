/*****************************************************************************/
/*                                                                           */
/* Copyright notice: please read file license.txt in the NetBee root folder. */
/*                                                                           */
/*****************************************************************************/

/****************************************************************************/
/*																		    */
/* class developed by Ivano Cerrato - wed nov 30 2011			   			*/
/*																		    */
/****************************************************************************/

#pragma once


#include "defs.h"
#include "symbols.h"
#include <math.h>
#include <set>

#define INF 65535

using namespace std;


class AssignProtocolLayer
{

	EncapGraph	&m_Graph;
	bool assigned;
	map<double,set<SymbolProto*> > pl; //for each layer, contains the set of the name of protocols of that layer
	
	void Initialize(void)
	{
		EncapGraph::NodeIterator i = m_Graph.FirstNode();
		for (; i != m_Graph.LastNode(); i++)
		{
			if((*i)->NodeInfo->ID!=0) //the node is not the startproto
				(*i)->NodeInfo->Level = INF;
			else
				(*i)->NodeInfo->Level = 1;
		} 
	
	}
	
	EncapGraph::GraphNode *GetMinSuccessor(EncapGraph::GraphNode &node)
	{
		EncapGraph::GraphNode *successor = NULL;
		double minLevel = INF;
		list<EncapGraph::GraphNode*> &successors = node.GetSuccessors();
		list<EncapGraph::GraphNode*>::iterator s = successors.begin();
		for (; s != successors.end(); s++)
		{
			if(*s == &node) //we must no consider this case
				continue;
			if ((*s)->NodeInfo->Level < minLevel)
			{
				minLevel = (*s)->NodeInfo->Level;
				successor = *s;
			}
		}
		return successor;
	}
	
	EncapGraph::GraphNode *GetMaxPredecessor(EncapGraph::GraphNode &node)
	{	
		EncapGraph::GraphNode *predecessor = NULL;
		double maxLevel = 0;
		list<EncapGraph::GraphNode*> &predecessors = node.GetPredecessors();
		list<EncapGraph::GraphNode*>::iterator p = predecessors.begin();
		for (; p != predecessors.end(); p++)
		{
			if(*p == &node) //we must no consider this case
				continue;
			if ((*p)->NodeInfo->Level >= maxLevel)
			{
				maxLevel = (*p)->NodeInfo->Level;
				predecessor = *p;
			}
		}
		return predecessor;
	}
	
	void DepthWalk(EncapGraph::GraphNode &node)
	{

		if (node.Visited)
			return;

		node.Visited = true;

		double nodeLevel = node.NodeInfo->Level;

		EncapGraph::GraphNode *minSuccessor = GetMinSuccessor(node);
		
		double nextLevel = (minSuccessor!=NULL) ? minSuccessor->NodeInfo->Level : INF;

		if (nextLevel <= nodeLevel)
		{
			EncapGraph::GraphNode *maxPredecessor = GetMaxPredecessor(node);
			nbASSERT(maxPredecessor != NULL, "maxPredecessor should not be NULL");
			double prevLevel = maxPredecessor->NodeInfo->Level;
			if (prevLevel < nextLevel)
			{
				double old = node.NodeInfo->Level;
				node.NodeInfo->Level = prevLevel + ((nextLevel - prevLevel) / 2);
				
				ChangeInformation(node.NodeInfo, old, node.NodeInfo->Level);
			}
		}

		double level = ceil(node.NodeInfo->Level + 1);
		list<EncapGraph::GraphNode*> &successors = node.GetSuccessors();
		list<EncapGraph::GraphNode*>::iterator i = successors.begin();
		for (; i != successors.end(); i++)
		{
			if (level < (*i)->NodeInfo->Level)
			{
				double old = (*i)->NodeInfo->Level;
				(*i)->NodeInfo->Level = level;
				
				ChangeInformation((*i)->NodeInfo, old, (*i)->NodeInfo->Level);				
			}
		}
		list<EncapGraph::GraphNode*>::iterator ss = successors.begin();
		for (; ss != successors.end(); ss++)
			DepthWalk(**ss);

	}
	
	void ChangeInformation(SymbolProto *protocol, double oldL, double newL)
	{
		//remove the old info - if needed
		map<double,set<SymbolProto*> >::iterator element = pl.find(oldL);
		set<SymbolProto*> s;
		if(element != pl.end())
		{
			s = (*element).second;
		
			set<SymbolProto*>::iterator n = s.begin();
			for(; n != s.end(); n++)
			{
				if((*n) == protocol)
					break;
			}
			if(n!=s.end())
				s.erase(n);		
			pl.erase(oldL);		
			pl.insert(make_pair<double,set<SymbolProto*> >(oldL,s));
		}
				
		//store the new info
		map<double,set<SymbolProto*> >:: iterator element2 = pl.find(newL);
		set<SymbolProto*> s2; 
		if(element2 != pl.end())
			s2 = (*element2).second;
		s2.insert(protocol);
		pl.erase(newL);
		pl.insert(make_pair<double,set<SymbolProto*> >(newL,s2));
	}

public:

	AssignProtocolLayer(EncapGraph &graph)
		:m_Graph(graph),assigned(false){}
	
	//assign a layer to each protocol of the database	
	void Classify(void)
	{
		if(!assigned)
		{
			Initialize();		
			m_Graph.ResetVisited();
			DepthWalk(m_Graph.GetStartProtoNode());
			assigned = true;
		}
	}
	
	//returns the protocols of a certain level
	set<SymbolProto*> GetProtocolsOfLayer(double layer)
	{
		nbASSERT(assigned,"You must call \"Classify()\" before this method!");
		
		if(layer==1)
		{
			set<SymbolProto*> aux;
			aux.insert((m_Graph.GetStartProtoNode()).NodeInfo);
			return aux;
		}
		
		map<double,set<SymbolProto*> >::iterator it = pl.find(layer);
		set<SymbolProto*> s = (*it).second;
		return (*(pl.find(layer))).second;
	}


};

