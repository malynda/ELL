////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Project:  Embedded Machine Learning Library (EMLL)
//  File:     CppCompiler.tcc (compiler)
//  Authors:  Umesh Madan
//
////////////////////////////////////////////////////////////////////////////////////////////////////
namespace emll
{
	namespace compiler
	{
		template<typename T>
		void CppCompiler::Emit(Variable& var)
		{
			if (var.IsScalar())
			{
				return EmitScalar<T>(var);
			}
			else if (var.IsVector())
			{
				return EmitVector<T>(var);
			}
			else
			{
				throw new CompilerException(CompilerError::variableTypeNotSupported);
			}
		}

		template<typename T>
		void CppCompiler::EmitScalar(Variable& var)
		{
			switch (var.Scope())
			{
				case VariableScope::literal:
					EmitLiteral<T>(static_cast<LiteralVar<T>&>(var));
					break;
				case VariableScope::local:
					if (var.IsVectorRef())
					{
						EmitRef<T>(static_cast<VectorElementVar<T>&>(var));
					}
					else if (var.HasInitValue())
					{
						EmitLocal<T>(static_cast<InitializedScalarVar<T>&>(var));
					}
					else
					{
						EmitLocal<T>(static_cast<ScalarVar<T>&>(var));
					}
					break;

				case VariableScope::global:
					EmitGlobal<T>(static_cast<InitializedScalarVar<T>&>(var));
					break;	

				case VariableScope::rValue:
					EmitRValue<T>(var);
					break;

				default:
					throw new CompilerException(CompilerError::variableScopeNotSupported);
			}
		}

		template<typename T>
		void CppCompiler::EmitVector(Variable& var)
		{
			switch (var.Scope())
			{
				case VariableScope::literal:
					EmitLiteralVector<T>(static_cast<LiteralVarV<T>&>(var));
					break;
				case VariableScope::global:
					if (var.HasInitValue())
					{
						EmitGlobalVector<T>(static_cast<InitializedVectorVar<T>&>(var));
					}
					else
					{
						EmitGlobalVector<T>(static_cast<VectorVar<T>&>(var));
					}
					break;
				default:
					throw new CompilerException(CompilerError::variableScopeNotSupported);
			}
		}

		template<typename T>
		void CppCompiler::EmitLocal(ScalarVar<T>& var)
		{
			_pfn->Var(var.Type(), var.EmittedName());
		}

		template<typename T>
		void CppCompiler::EmitLocal(InitializedScalarVar<T>& var)
		{
			_pfn->Var<T>(var.EmittedName(), var.Data());
		}

		template<typename T>
		void CppCompiler::EmitLiteral(LiteralVar<T>& var)
		{			
			_pfn->Literal(var.Data());
		}

		template<typename T>
		void CppCompiler::EmitRef(VectorElementVar<T>& var)
		{
			EnsureEmitted(var.Src());
			_pfn->ValueAt(var.Src().EmittedName(), var.Offset());
		}

		template<typename T>
		void CppCompiler::EmitGlobal(InitializedScalarVar<T>& var)
		{
			if (var.IsMutable())
			{
				_module.Global<T>(var.EmittedName(), var.Data());
			}
			else
			{
				_module.Constant<T>(var.EmittedName(), var.Data());
			}
		}

		template<typename T>
		void CppCompiler::EmitLiteralVector(LiteralVarV<T>& var)
		{
			_module.ConstantV<T>(var.EmittedName(), var.Data());
		}

		template<typename T>
		void CppCompiler::EmitGlobalVector(VectorVar<T>& var)
		{
			_module.GlobalV<T>(var.EmittedName(), var.Dimension());
		}

		template<typename T>
		void CppCompiler::EmitGlobalVector(InitializedVectorVar<T>& var)
		{
			_module.GlobalV<T>(var.EmittedName(), var.Data());
		}

		template<typename T>
		void CppCompiler::EmitRValue(Variable& var)
		{
			//_pfn->Call(var.EmittedName(), InputArgs());
		}

		template<typename T>
		void CppCompiler::CompileOutput(const model::OutputNode<T>& node)
		{
			// Output ports have exactly 1 input, output
			auto pInput = node.GetInputPorts()[0];
			Variable* pOutputVar = EnsureVariableFor(node.GetOutputPorts()[0]);
			for (size_t i = 0; i < pInput->Size(); ++i)
			{
				auto outputElt = pInput->GetInputElement(i);
				_pfn->AssignValueAt(pOutputVar->EmittedName(), i, [&outputElt, this]() {LoadVar(outputElt); });
			}
		}

		template<typename T>
		void CppCompiler::CompileBinary(const nodes::BinaryOperationNode<T>& node)
		{
			NewCodeBlock(node);

			auto pInput1 = node.GetInputPorts()[0];
			auto pInput2 = node.GetInputPorts()[1];
			if ((ModelEx::IsPureVector(*pInput1) && ModelEx::IsPureVector(*pInput2)) &&
				!Settings().ShouldUnrollLoops())
			{
				CompileBinaryLoop<T>(node);
			}
			else
			{
				CompileBinaryExpanded<T>(node);
			}

			TryMergeCodeBlock(node);
		}

		template<typename T>
		void CppCompiler::CompileBinaryLoop(const nodes::BinaryOperationNode<T>& node)
		{
			Variable* pLVector = EnsureEmitted(node.GetInputPorts()[0]);
			Variable* pRVector = EnsureEmitted(node.GetInputPorts()[1]);
			auto pOutput = node.GetOutputPorts()[0];
			Variable* pResultVector = EnsureEmitted(pOutput);
			auto iVarName = LoopVarName();
			_pfn->For(iVarName, pOutput->Size());
			{
				_pfn->AssignValueAt(pResultVector->EmittedName(), iVarName);
				_pfn->Op(GetOperator<T>(node),
					[&pLVector, &iVarName, this]() {_pfn->ValueAt(pLVector->EmittedName(), iVarName); },
					[&pRVector, &iVarName, this]() {_pfn->ValueAt(pRVector->EmittedName(), iVarName); });
				_pfn->EndStatement();
			}
			_pfn->EndFor();
		}

		template<typename T>
		void CppCompiler::CompileBinaryExpanded(const nodes::BinaryOperationNode<T>& node)
		{
			auto pInput1 = node.GetInputPorts()[0];
			auto pInput2 = node.GetInputPorts()[1];
			auto pOutput = node.GetOutputPorts()[0];
			Variable& resultVar = *(EnsureEmitted(pOutput));
			for (size_t i = 0; i < pInput1->Size(); ++i)
			{
				auto lInput = pInput1->GetInputElement(i);
				auto rInput = pInput2->GetInputElement(i);
				_pfn->AssignValue(resultVar, i);
				{
					_pfn->Op(GetOperator<T>(node), [&lInput, this]() {LoadVar(lInput); },[&rInput, this]() {LoadVar(rInput); });
				}
				_pfn->EndStatement();
			}
		}

		template<typename T>
		void CppCompiler::CompileSum(const nodes::SumNode<T>& node)
		{
			NewCodeBlock(node);

			// SumNode has exactly 1 input and 1 output
			auto input = node.GetInputPorts()[0];
			if (ModelEx::IsPureVector(*input) &&
				!Settings().ShouldUnrollLoops())
			{
				CompileSumLoop<T>(node);
			}
			else
			{
				CompileSumExpanded<T>(node);
			}

			TryMergeCodeBlock(node);
		}

		template<typename T>
		void CppCompiler::CompileSumLoop(const nodes::SumNode<T>& node)
		{
			auto pInput = node.GetInputPorts()[0];
			auto pOutput = node.GetOutputPorts()[0];
			Variable* pSrcVector = EnsureEmitted(pInput);
			Variable& resultVar = *(EnsureEmitted(pOutput));
			auto iVarName = LoopVarName();
			_pfn->Assign(resultVar.EmittedName(), GetDefaultForValueType<T>());
			_pfn->For(LoopVarName(), pSrcVector->Dimension());
			{
				_pfn->IncrementUpdate(resultVar.EmittedName());
				{
					_pfn->ValueAt(pSrcVector->EmittedName(), iVarName);
				}
				_pfn->EndStatement();
			}
			_pfn->EndFor();
		}

		template<typename T>
		void CppCompiler::CompileSumExpanded(const nodes::SumNode<T>& node)
		{
			auto pInput = node.GetInputPorts()[0];
			auto pOutput = node.GetOutputPorts()[0];
			Variable& resultVar = *(EnsureEmitted(pOutput));

			_pfn->Assign(resultVar.EmittedName(), GetDefaultForValueType<T>());
			for (size_t i = 0; i < pInput->Size(); ++i)
			{
				auto pRInput = pInput->GetInputElement(i);
				_pfn->IncrementUpdate(resultVar.EmittedName());
				{
					LoadVar(pRInput);
				}
				_pfn->EndStatement();
			}
		}

		template<typename T>
		void CppCompiler::CompileDotProduct(const nodes::DotProductNode<T>& node)
		{
			NewCodeBlock(node);

			auto pInput1 = node.GetInputPorts()[0];
			auto pInput2 = node.GetInputPorts()[1];
			if ((ModelEx::IsPureVector(*pInput1) && ModelEx::IsPureVector(*pInput2)) &&
				!Settings().ShouldUnrollLoops())
			{
				CompileDotProductLoop<T>(node);
			}
			else
			{
				CompileDotProductExpanded<T>(node);
			}

			TryMergeCodeBlock(node);
		}

		template<typename T>
		void CppCompiler::CompileDotProductLoop(const nodes::DotProductNode<T>& node)
		{
			Variable* pLVector = EnsureEmitted(node.GetInputPorts()[0]);
			Variable* pRVector = EnsureEmitted(node.GetInputPorts()[1]);
			auto pOutput = node.GetOutputPorts()[0];
			Variable* pResult = EnsureEmitted(pOutput);
			_pfn->Assign(pResult->EmittedName(), 0);

			auto iVarName = LoopVarName();
			_pfn->For(iVarName, pOutput->Size());
			{
				_pfn->IncrementUpdate(pResult->EmittedName()).
					Op(GetMultiplyForValueType<T>(),
						[&pLVector, &iVarName, this]() {_pfn->ValueAt(pLVector->EmittedName(), iVarName); },
						[&pRVector, &iVarName, this]() {_pfn->ValueAt(pRVector->EmittedName(), iVarName); })
					.EndStatement();
			}
			_pfn->EndFor();
		}

		template<typename T>
		void CppCompiler::CompileDotProductExpanded(const nodes::DotProductNode<T>& node)
		{
			auto pInput1 = node.GetInputPorts()[0];
			auto pInput2 = node.GetInputPorts()[1];
			auto pOutput = node.GetOutputPorts()[0];
			Variable& resultVar = *(EnsureEmitted(pOutput));
			_pfn->Assign(resultVar.EmittedName(), 0);
			for (size_t i = 0; i < pInput1->Size(); ++i)
			{
				auto lInput = pInput1->GetInputElement(i);
				auto rInput = pInput2->GetInputElement(i);
				_pfn->IncrementUpdate(resultVar.EmittedName())
					 .Op(GetMultiplyForValueType<T>(), [&lInput, this]() {LoadVar(lInput); }, [&rInput, this]() {LoadVar(rInput); })
					 .EndStatement();
			}
		}

		template<typename T>
		void CppCompiler::CompileAccumulator(const nodes::AccumulatorNode<T>& node)
		{
			// AccumulatorNode has exactly 1 input and 1 output
			// Accumulators are always long lived - either globals or heap. Currently, we use globals
			auto pInput = node.GetInputPorts()[0];
			auto pOutput = node.GetOutputPorts()[0];
			Variable* pVar = Variables().AddVectorVariable(VariableScope::global, GetValueType<T>(), pOutput->Size());
			SetVariableFor(pOutput, pVar);

			if (ModelEx::IsPureVector(*pInput) &&
				!Settings().ShouldUnrollLoops())
			{
				CompileAccumulatorLoop<T>(node);
			}
			else
			{
				CompileAccumulatorExpanded<T>(node);
			}
		}

		template<typename T>
		void CppCompiler::CompileAccumulatorLoop(const nodes::AccumulatorNode<T>& node)
		{
			auto pInput = node.GetInputPorts()[0];
			auto pOutput = node.GetOutputPorts()[0];
			Variable* pInputVector = EnsureEmitted(pInput);
			Variable* pAccumulatorVector = EnsureEmitted(pOutput);

			auto iVarName = LoopVarName();
			_pfn->For(iVarName, pOutput->Size());
			{
				_pfn->IncrementValueAt(pAccumulatorVector->EmittedName(), iVarName)
					 .ValueAt(pInputVector->EmittedName(), iVarName)
					 .EndStatement();
			}
			_pfn->EndFor();
		}

		template<typename T>
		void CppCompiler::CompileAccumulatorExpanded(const nodes::AccumulatorNode<T>& node)
		{
			auto pInput = node.GetInputPorts()[0];
			auto pOutput = node.GetOutputPorts()[0];
			Variable* pAccumulatorVector = EnsureEmitted(pOutput);

			for (size_t i = 0; i < pInput->Size(); ++i)
			{
				_pfn->IncrementValueAt(pAccumulatorVector->EmittedName(), i);
				{
					LoadVar(pInput->GetInputElement(i));
				}
				_pfn->EndStatement();
			}
		}

		template<typename T>
		void CppCompiler::CompileBinaryPredicate(const nodes::BinaryPredicateNode<T>& node)
		{
			NewCodeBlock(node);

			// Binary predicate has 2 inputs and 1 output
			auto pInput1 = node.GetInputPorts()[0];
			auto pInput2 = node.GetInputPorts()[1];
			auto pOutput = node.GetOutputPorts()[0];
			VerifyIsScalar(*pInput1);
			VerifyIsScalar(*pInput2);
			VerifyIsScalar(*pOutput);

			Variable& resultVar = *(EnsureEmitted(pOutput));
			auto lInput = pInput1->GetInputElement(0);
			auto rInput = pInput2->GetInputElement(0);
			_pfn->Assign(resultVar.EmittedName());
			_pfn->Cmp(GetComparison<T>(node), [&lInput, this](){LoadVar(lInput); }, [&rInput, this](){LoadVar(rInput); });
			_pfn->EndStatement();

			TryMergeCodeBlock(node);
		}

		template<typename T, typename SelectorType>
		void CppCompiler::CompileMultiplexer(const nodes::MultiplexerNode<T, SelectorType>& node)
		{
			// Only support binary right now
			VerifyIsPureBinary(node);

			NewCodeBlock(node);

			auto pElements = node.GetInputPorts()[0];
			CompileMultiplexerBinary<T, SelectorType>(node);
		}

		///<summary>Compile an element selector node</summary>
		template<typename T, typename SelectorType>
		void CppCompiler::CompileMultiplexerBinary(const nodes::MultiplexerNode<T, SelectorType>& node)
		{
			auto pElements = node.GetInputPorts()[0];
			auto pSelector = node.GetInputPorts()[1];
			VerifyIsScalar(*pSelector);

			auto pOutput = node.GetOutputPorts()[0];
			VerifyIsScalar(*pOutput);

			Variable* pResult = EnsureEmitted(pOutput);
			auto lVal = pElements->GetInputElement(1);  // lval is selected if the result of the "if" comparison is NON-zero
			auto rVal = pElements->GetInputElement(0);
			auto lMergeableSrc = GetMergeableNode(lVal);
			auto rMergeableSrc = GetMergeableNode(rVal);
			if (lMergeableSrc == nullptr && rMergeableSrc == nullptr)
			{
				_pfn->Assign(pResult->EmittedName());
				_pfn->IfInline([&pSelector, this]() { LoadVar(pSelector); }, [&lVal, this]() { LoadVar(lVal); }, [&rVal, this]() { LoadVar(rVal); });
			}
			else
			{
				_pfn->If([&pSelector, this]() { LoadVar(pSelector); });
				{
					if (lMergeableSrc != nullptr)
					{
						TryMergeNodeIntoBlock(_pfn->CurrentBlock(), *lMergeableSrc);
					}
					_pfn->Assign(pResult->EmittedName());
					LoadVar(lVal);
					_pfn->EndStatement();
				}
				_pfn->EndIf();
				_pfn->Else();
				{
					if (rMergeableSrc != nullptr)
					{
						TryMergeNodeIntoBlock(_pfn->CurrentBlock(), *rMergeableSrc);
					}
					_pfn->Assign(pResult->EmittedName());
					LoadVar(rVal);
					_pfn->EndStatement();
				}
				_pfn->EndIf();
			}

			auto pSelectorNode = pSelector->GetParentNodes()[0];
			if (ModelEx::HasSingleDescendant(*pSelectorNode))
			{
				TryMergeNodeBlocks(*pSelectorNode, node);
			}
		}
	}
}