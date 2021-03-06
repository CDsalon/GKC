﻿/*
** Copyright (c) 2015, Xin YUAN, courses of Zhejiang University
** All rights reserved.
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the 2-Clause BSD License.
**
** Author contact information:
**   yxxinyuan@zju.edu.cn
**
*/

////////////////////////////////////////////////////////////////////////////////
#ifndef __LEXER_H__
#define __LEXER_H__
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
namespace GKC {
////////////////////////////////////////////////////////////////////////////////

// TokenTable

class TokenTable
{
public:
	TokenTable() throw() : m_string_pool(RefPtrHelper::TypeCast<ArrayPoolAllocator, IMemoryAllocatorRef32>(RefPtr<ArrayPoolAllocator>(m_str_allocator))),
						m_symbol_pool(m_string_pool, RefPtrHelper::TypeCast<ArrayPoolAllocator, IMemoryAllocatorRef32>(RefPtr<ArrayPoolAllocator>(m_sym_allocator))),
						m_uLevelHead(0)
	{
	}
	~TokenTable() throw()
	{
	}

	//remove all nodes
	void RemoveAll() throw()
	{
		m_sym_allocator.Clear();
		m_str_allocator.Clear();
		m_symbol_pool.Reset();
		//m_string_pool
		m_uLevelHead = 0;
	}

	void InsertToken(const ConstStringA& strToken, uint uID)
	{
		assert( uID >= CPL_TK_FIRST );
		assert( m_symbol_pool.Find(strToken).IsNull() );
		auto iter(m_symbol_pool.CreateNode(strToken, sizeof(uint), 0, 0, m_uLevelHead));  //may throw
		iter.GetData<BeType<uint>>().set_Value(uID);
		m_symbol_pool.SetZeroLevelHead(m_uLevelHead);
	}

	//properties
	uint get_ID(const ConstStringA& strToken) const throw()
	{
		auto iter(m_symbol_pool.Find(strToken));
		if( iter.IsNull() )
			return 0;
		return iter.GetData<BeType<uint>>().get_Value();
	}

private:
	ArrayPoolAllocator m_sym_allocator;
	ArrayPoolAllocator m_str_allocator;

	SymbolPool m_symbol_pool;
	StringPool m_string_pool;

	uint m_uLevelHead;  //not used in this class

private:
	//noncopyable
	TokenTable(const TokenTable&) throw();
	TokenTable& operator=(const TokenTable&) throw();
};

// LexerParser

class LexerParser
{
public:
	LexerParser() throw()
	{
	}
	~LexerParser() throw()
	{
	}

	//settings
	void SetTokenTable(const RefPtr<TokenTable>& tk) throw()
	{
		m_token_table = tk;
	}
	void SetFsaTable(const FSA_TABLE& tb) throw()
	{
		m_fsa.SetTable(tb);
	}
	void SetAction(const ConstStringA& strToken, const WeakCom<_ILexerAction>& spAction)
	{
		if( m_arrAction.IsBlockNull() )
			m_arrAction = ShareArrayHelper::MakeShareArray<WeakCom<_ILexerAction>>(MemoryHelper::GetCrtMemoryManager());  //may throw
		//find id
		uint uID = m_token_table.Deref().get_ID(strToken);
		assert( uID > 0 );
		//fill
		uint uLeastCount = SafeOperators::AddThrow(uID, (uint)1);  //may throw
		if( m_arrAction.GetCount() < (uintptr)uLeastCount )
			m_arrAction.SetCount((uintptr)uLeastCount, 0);  //may throw
		m_arrAction.SetAt(uID, spAction);
	}
	void SetStream(IN const ShareCom<ITextStream>& stream) throw()
	{
		m_stream = stream;
	}
	ShareCom<ITextStream> GetStream() const throw()
	{
		return m_stream;
	}

	//token info
	_LexerTokenInfo& GetTokenInfo() throw()
	{
		return m_token_info;
	}

	//start parsing
	void Start() throw()
	{
		m_fsa.InitializeAsStopState();  //special
		m_token_info.ResetCharInfo();
	}

	// called repeatedly.
	// return : SystemCallResults::OK, the token id may be CPL_TK_ERROR.
	//          SystemCallResults::S_False, it reaches the end of stream.
	//          otherwise, this call is failed.
	CallResult Parse()
	{
		CallResult cr;

		bool bErrorOrEnd;
		//can restart (last token is error or EOE)
		if( !m_fsa.CanRestart(bErrorOrEnd) ) {
			cr.SetResult(bErrorOrEnd ? SystemCallResults::Fail : SystemCallResults::S_False);
			return cr;
		}

		uint uEvent;
		CharA ch;
		//read a character
		cr = m_stream.Deref().GetCharA(ch);
		if( cr.IsFailed() )
			return cr;
		if( cr.GetResult() == SystemCallResults::S_EOF ) {
			cr.SetResult(SystemCallResults::S_False);
			return cr;
		}

		//start state
		m_fsa.SetStartState();
		m_token_info.Reset();  //may throw
		//first character
		uEvent = ch;
		m_token_info.Append(ch);  //may throw

		//loop
		do {
			//state
			m_fsa.ProcessState(uEvent);
			//stopped
			if( m_fsa.IsStopped() ) {
				//match
				uint uBackNum;
				int iMatch = m_fsa.GetMatch(uBackNum);
				assert( iMatch >= 0 );
				if( iMatch == 0 ) {
					//error
					m_token_info.set_ID(CPL_TK_ERROR);
					break;
				}
				//iMatch is the same as token id (from CPL_TK_FIRST)
				m_token_info.set_ID(iMatch);
				//back
				if( uBackNum > 0 ) {
					cr = m_stream.Deref().UngetCharA((int64)uBackNum);
					assert( cr.IsOK() );
					m_token_info.BackChar(uBackNum);  //may throw
				}
				//action
				ShareCom<_ILexerAction> action(find_action(m_token_info.get_ID()));
				if( !action.IsBlockNull() )
					cr = action.Deref().DoAction(m_stream, m_token_info);
				break;
			} //end if
			//get next char
			cr = m_stream.Deref().GetCharA(ch);
			if( cr.IsFailed() )
				break;
			if( cr.GetResult() == SystemCallResults::S_EOF ) {
				uEvent = FSA_END_OF_EVENT;
				cr.SetResult(SystemCallResults::OK);
			}
			else {
				uEvent = ch;
				m_token_info.Append(ch);  //may throw
			}
		} while( true );

		return cr;
	}

private:
	ShareCom<_ILexerAction> find_action(uint uID) const throw()
	{
		ShareCom<_ILexerAction> ret;
		if( m_arrAction.IsBlockNull() || (uintptr)uID >= m_arrAction.GetCount() )
			return ret;
		ret = ShareComHelper::ToShareCom(m_arrAction[uID].get_Value());
		return ret;
	}

private:
	//settings
	RefPtr<TokenTable>   m_token_table;
	FiniteStateAutomata  m_fsa;
	ShareArray<WeakCom<_ILexerAction>>  m_arrAction;
	ShareCom<ITextStream>  m_stream;

	//token info
	_LexerTokenInfo  m_token_info;

private:
	//noncopyable
	LexerParser(const LexerParser&) throw();
	LexerParser& operator=(const LexerParser&) throw();
};

// private interfaces

// _ILexerTablesAccess

class NOVTABLE _ILexerTablesAccess
{
public:
	virtual RefPtr<TokenTable> GetTokenTable() throw() = 0;
	virtual const FSA_TABLE& GetFsaTable() throw() = 0;
};

DECLARE_GUID(GUID__ILexerTablesAccess)

// _ILexerAnalyzerAccess

class NOVTABLE _ILexerAnalyzerAccess
{
public:
	virtual RefPtr<LexerParser> GetLexerParser() throw() = 0;
};

DECLARE_GUID(GUID__ILexerAnalyzerAccess)

////////////////////////////////////////////////////////////////////////////////
}
////////////////////////////////////////////////////////////////////////////////
#endif //__LEXER_H__
////////////////////////////////////////////////////////////////////////////////
