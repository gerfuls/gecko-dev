/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express oqr
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is the JavaScript 2 Prototype.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU Public License (the "GPL"), in which case the
 * provisions of the GPL are applicable instead of those above.
 * If you wish to allow use of your version of this file only
 * under the terms of the GPL and not to allow others to use your
 * version of this file under the NPL, indicate your decision by
 * deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL.  If you do not delete
 * the provisions above, a recipient may use your version of this
 * file under either the NPL or the GPL.
 */

#ifndef icodegenerator_h
#define icodegenerator_h

#include <vector>
#include <stack>
#include <iostream>

#include "utilities.h"
#include "parser.h"
#include "vmtypes.h"

namespace JavaScript {
    namespace ICG {

        using namespace VM;
        
        class ICodeGenerator; // forward declaration
    
        enum StateKind {
            While_state,
            If_state,
            Do_state,
            Switch_state,
            For_state
        };

        class ICodeState {
        public :
            ICodeState(StateKind kind, ICodeGenerator *icg); // inline below
            virtual ~ICodeState()   { }

            virtual Label *getBreakLabel(ICodeGenerator *) \
                { ASSERT(false); return NULL; }
            virtual Label *getContinueLabel(ICodeGenerator *) \
                { ASSERT(false); return NULL;}

            StateKind stateKind;
            Register registerBase;
            Label *breakLabel;
            Label *continueLabel;
        };

        class ICodeModule {
        public:
            ICodeModule(InstructionStream *iCode, uint32 maxRegister,
                        uint32 maxVariable) :
                its_iCode(iCode), itsMaxRegister(maxRegister),
                itsMaxVariable(maxVariable) { }

            InstructionStream *its_iCode;
            uint32  itsMaxRegister;
            uint32  itsMaxVariable;
        };

        /****************************************************************/
        
        // An ICodeGenerator provides the interface between the parser and the
        // interpreter. The parser constructs one of these for each
        // function/script, adds statements and expressions to it and then
        // converts it into an ICodeModule, ready for execution.

        class ICodeGenerator {
        private:
            InstructionStream *iCode;
            LabelList labels;
            std::vector<ICodeState *> stitcher;

            void markMaxRegister() \
                { if (topRegister > maxRegister) maxRegister = topRegister; }
            void markMaxVariable(uint32 variableIndex) \
                { if (variableIndex > maxVariable) maxVariable = variableIndex; }

            Register topRegister;
            Register getRegister() \
                { return topRegister++; }
            void resetTopRegister() \
                { markMaxRegister(); topRegister = stitcher.empty() ? 0 : \
                    stitcher.back()->registerBase; }

            ICodeOp getBranchOp() \
                { ASSERT(!iCode->empty()); return iCode->back()->getBranchOp(); }

            uint32  maxRegister;
            uint32  maxVariable;
        
            void setLabel(Label *label);
            void setLabel(InstructionStream *stream, Label *label);

            void branch(Label *label);
            void branchConditional(Label *label, Register condition);
    
        public:
            ICodeGenerator() : topRegister(0), maxRegister(0),
                               maxVariable(0) \
            { iCode = new InstructionStream(); }

            virtual ~ICodeGenerator() { if (iCode) delete iCode; }

            void mergeStream(InstructionStream *sideStream);
        
            ICodeModule *complete();

            Formatter& print(Formatter& f);

            Register op(ICodeOp op, Register source);
            Register op(ICodeOp op, Register source1, Register source2);
            Register call(Register target, RegisterList args);

            Register compare(ICodeOp op, Register source1, Register source2);
 
            Register loadVariable(uint32 frameIndex);
            Register loadImmediate(double value);

            void saveVariable(uint32 frameIndex, Register value);

            Register newObject();
            Register newArray();

            Register loadName(StringAtom &name);
            void saveName(StringAtom &name, Register value);
        
            Register getProperty(Register base, StringAtom &name);
            void setProperty(Register base, StringAtom &name, Register value);

            Register getElement(Register base, Register index);
            void setElement(Register base, Register index, Register value);

            Register getRegisterBase() { return topRegister; }
            InstructionStream *get_iCode() { return iCode; }

            Label *getLabel();


            // Rather than have the ICG client maniplate labels and branches, it
            // uses the following calls to describe the high level looping
            // constructs being generated. The functions listed below are
            // expected to be called in the order listed for each construct,
            // (internal error otherwise).
            // The ICG will enforce correct nesting and closing.

            // expression statements
            void beginStatement(uint32 /*pos*/) { resetTopRegister(); }
    
            void returnStatement() { iCode->push_back(new Return()); }
            void returnStatement(Register result) \
                { iCode->push_back(new Return(result)); }

            void beginWhileStatement(uint32 pos);
            void endWhileExpression(Register condition);
            void endWhileStatement();

            void beginDoStatement(uint32 pos);
            void endDoStatement();
            void endDoExpression(Register condition);

            void beginIfStatement(uint32 pos, Register condition);
            void beginElseStatement(bool hasElse); // required, regardless of
                                                   // existence of else clause
            void endIfStatement();

            // for ( ... in ...) statements get turned into generic for
            // statements by the parser (ok?)
            void beginForStatement(uint32 pos); // for initialization is
                                                // emitted prior to this call
            void forCondition(Register condition); // required
            void forIncrement();                   // required
            void endForStatement();

            void beginSwitchStatement(uint32 pos, Register expression);

            void endCaseCondition(Register expression);
    
            void beginCaseStatement();
            void endCaseStatement();
        
            // optionally
            void beginDefaultStatement();
            void endDefaultStatement();

            void endSwitchStatement();

            void labelStatement(const StringAtom &label); // adds to label set
            // for next statement, removed when that statement is finished
            void continueStatement();
            void breakStatement();

            void continueStatement(const StringAtom &label);
            void breakStatement(const StringAtom &label);

            void throwStatement(Register expression);

            void beginCatchStatement();
            void endCatchExpression(Register expression);
            void endCatchStatement();

        };

        Formatter& operator<<(Formatter &f, ICodeGenerator &i);
        /*
        std::ostream &operator<<(std::ostream &s, ICodeGenerator &i);
        std::ostream &operator<<(std::ostream &s, StringAtom &str);
        */

        class WhileCodeState : public ICodeState {
        public:
            WhileCodeState(Label *conditionLabel, Label *bodyLabel,
                           ICodeGenerator *icg); // inline below
            InstructionStream *swapStream(InstructionStream *iCode) \
                { InstructionStream *t = whileExpressionStream; \
                whileExpressionStream = iCode; return t; }

            virtual Label *getBreakLabel(ICodeGenerator *icg) \
                { if (breakLabel == NULL) breakLabel = icg->getLabel(); \
                return breakLabel; }
            virtual Label *getContinueLabel(ICodeGenerator *) \
                { return whileCondition; }

            Label *whileCondition;
            Label *whileBody;
            InstructionStream *whileExpressionStream;
        };

        class ForCodeState : public ICodeState {
        public:
            ForCodeState(Label *conditionLabel, Label *bodyLabel,
                         ICodeGenerator *icg); // inline below
            InstructionStream *swapStream(InstructionStream *iCode) \
                { InstructionStream *t = forConditionStream; \
                forConditionStream = iCode; return t; }
            InstructionStream *swapStream2(InstructionStream *iCode) \
                { InstructionStream *t = forIncrementStream; \
                forIncrementStream = iCode; return t; }
        
            virtual Label *getBreakLabel(ICodeGenerator *icg) \
                { if (breakLabel == NULL) breakLabel = icg->getLabel(); \
                return breakLabel; }
            virtual Label *getContinueLabel(ICodeGenerator *) \
                { ASSERT(continueLabel); return continueLabel; }

            Label *forCondition;
            Label *forBody;
            InstructionStream *forConditionStream;
            InstructionStream *forIncrementStream;
        };

        class IfCodeState : public ICodeState {
        public:
            IfCodeState(Label *a, Label *b, ICodeGenerator *icg) 
                : ICodeState(If_state, icg), elseLabel(a), beyondElse(b) { }
            Label *elseLabel;
            Label *beyondElse;
        };

        class DoCodeState : public ICodeState {
        public:
            DoCodeState(Label *bodyLabel, Label *conditionLabel, 
                        ICodeGenerator *icg) 
                : ICodeState(Do_state, icg), doBody(bodyLabel),
                  doCondition(conditionLabel) { }

            virtual Label *getBreakLabel(ICodeGenerator *icg) \
                { if (breakLabel == NULL) breakLabel = icg->getLabel();
                return breakLabel; }
            virtual Label *getContinueLabel(ICodeGenerator *) \
                { return doCondition; }

            Label *doBody;
            Label *doCondition;
        };

        class SwitchCodeState : public ICodeState {
        public:
            SwitchCodeState(Register control, ICodeGenerator *icg); // inline below
            InstructionStream *swapStream(InstructionStream *iCode) \
                { InstructionStream *t = caseStatementsStream; \
                caseStatementsStream = iCode; return t; }
        
            virtual Label *getBreakLabel(ICodeGenerator *icg) \
                { if (breakLabel == NULL) breakLabel = icg->getLabel(); 
                return breakLabel; }

            Register controlExpression;
            Label *defaultLabel;
            InstructionStream *caseStatementsStream;
        };

        inline ICodeState::ICodeState(StateKind kind, ICodeGenerator *icg) 
            : stateKind(kind), registerBase(icg->getRegisterBase()),
              breakLabel(NULL), continueLabel(NULL) { }

        inline SwitchCodeState::SwitchCodeState(Register control,
                                                ICodeGenerator *icg)
            : ICodeState(Switch_state, icg), controlExpression(control),
              defaultLabel(NULL), caseStatementsStream(icg->get_iCode()) {}

        inline WhileCodeState::WhileCodeState(Label *conditionLabel,
                                              Label *bodyLabel,
                                              ICodeGenerator *icg) 
            : ICodeState(While_state, icg), whileCondition(conditionLabel),
              whileBody(bodyLabel), whileExpressionStream(icg->get_iCode()) {}

        inline ForCodeState::ForCodeState(Label *conditionLabel, 
                                          Label *bodyLabel, ICodeGenerator *icg) 
            : ICodeState(For_state, icg), forCondition(conditionLabel),
              forBody(bodyLabel), forConditionStream(icg->get_iCode()),
              forIncrementStream(icg->get_iCode()) {}

    }; /* namespace IGC */
    
}; /* namespace JavaScript */

#endif /* icodegenerator_h */
