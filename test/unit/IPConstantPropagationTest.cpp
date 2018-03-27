/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "IPConstantPropagation.h"

#include <gtest/gtest.h>

#include "ConstantPropagationRuntimeAssert.h"
#include "Creators.h"
#include "DexUtil.h"
#include "IPConstantPropagationAnalysis.h"
#include "IRAssembler.h"
#include "RedexTest.h"
#include "Walkers.h"

using namespace constant_propagation;
using namespace constant_propagation::interprocedural;

struct InterproceduralConstantPropagationTest : public RedexTest {};

bool operator==(const ConstantEnvironment& a, const ConstantEnvironment& b) {
  return a.equals(b);
}

bool operator==(const ArgumentDomain& a, const ArgumentDomain& b) {
  return a.equals(b);
}

// For some reason, gtest won't print your value with the `<<` operator if it
// is defined on a superclass. So we explicitly define and call the superclass
// implementation here.
std::ostream& operator<<(std::ostream& o, const SignedConstantDomain& scd) {
  return o << static_cast<ReducedProductAbstractDomain<SignedConstantDomain,
                                                       sign_domain::Domain,
                                                       ConstantDomain>>(scd);
}

TEST_F(InterproceduralConstantPropagationTest, constantArgument) {
  // Let bar() be the only method calling baz(I)V, passing it a constant
  // argument. baz() should be optimized for that constant argument.

  Scope scope;
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto m1 = assembler::method_from_string(R"(
    (method (public) "LFoo;.bar:()V"
     (
      (load-param v0) ; the `this` argument
      (const v1 0)
      (invoke-direct (v0 v1) "LFoo;.baz:(I)V")
      (return-void)
     )
    )
  )");
  m1->rstate.set_keep();
  creator.add_method(m1);

  auto m2 = assembler::method_from_string(R"(
    (method (private) "LFoo;.baz:(I)V"
     (
      (load-param v0) ; the `this` argument
      (load-param v1)
      (if-eqz v1 :label)
      (const v0 0)
      (:label)
      (return-void)
     )
    )
  )");
  creator.add_method(m2);

  auto cls = creator.create();
  scope.push_back(cls);
  InterproceduralConstantPropagationPass().run(scope);

  auto expected_code2 = assembler::ircode_from_string(R"(
    (
     (load-param v0)
     (load-param v1)
     (goto :label)
     (const v0 0)
     (:label)
     (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(m2->get_code()),
            assembler::to_s_expr(expected_code2.get()));
}

TEST_F(InterproceduralConstantPropagationTest, nonConstantArgument) {
  // Let there be two methods calling baz(I)V, passing it different arguments.
  // baz() cannot be optimized for a constant argument here.

  Scope scope;
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto m1 = assembler::method_from_string(R"(
    (method (public) "LFoo;.foo:()V"
     (
      (load-param v0) ; the `this` argument
      (const v1 0)
      (invoke-direct (v0 v1) "LFoo;.baz:(I)V")
      (return-void)
     )
    )
  )");
  m1->rstate.set_keep();
  creator.add_method(m1);

  auto m2 = assembler::method_from_string(R"(
    (method (public) "LFoo;.bar:()V"
     (
      (load-param v0) ; the `this` argument
      (const v1 1)
      (invoke-direct (v0 v1) "LFoo;.baz:(I)V")
      (return-void)
     )
    )
  )");
  m2->rstate.set_keep();
  creator.add_method(m2);

  auto m3 = assembler::method_from_string(R"(
    (method (private) "LFoo;.baz:(I)V"
     (
      (load-param v0) ; the `this` argument
      (load-param v1)
      (if-eqz v1 :label)
      (const v0 0)
      (:label)
      (return-void)
     )
    )
  )");
  creator.add_method(m3);

  auto cls = creator.create();
  scope.push_back(cls);

  // m3's code should be unchanged since it cannot be optimized
  auto expected = assembler::to_s_expr(m3->get_code());
  InterproceduralConstantPropagationPass().run(scope);
  EXPECT_EQ(assembler::to_s_expr(m3->get_code()), expected);
}

TEST_F(InterproceduralConstantPropagationTest, argumentsGreaterThanZero) {
  // Let baz(I)V always be called with arguments > 0. baz() should be
  // optimized for that scenario.

  Scope scope;
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto m1 = assembler::method_from_string(R"(
    (method (public) "LFoo;.bar:()V"
     (
      (load-param v0) ; the `this` argument
      (const v1 1)
      (invoke-direct (v0 v1) "LFoo;.baz:(I)V")
      (return-void)
     )
    )
  )");
  m1->rstate.set_keep();
  creator.add_method(m1);

  auto m2 = assembler::method_from_string(R"(
    (method (public) "LFoo;.bar2:()V"
     (
      (load-param v0) ; the `this` argument
      (const v1 2)
      (invoke-direct (v0 v1) "LFoo;.baz:(I)V")
      (return-void)
     )
    )
  )");
  m2->rstate.set_keep();
  creator.add_method(m2);

  auto m3 = assembler::method_from_string(R"(
    (method (private) "LFoo;.baz:(I)V"
     (
      (load-param v0) ; the `this` argument
      (load-param v1)
      (if-gtz v1 :label)
      (const v0 0)
      (:label)
      (return-void)
     )
    )
  )");
  creator.add_method(m3);

  auto cls = creator.create();
  scope.push_back(cls);
  InterproceduralConstantPropagationPass().run(scope);

  auto expected_code3 = assembler::ircode_from_string(R"(
    (
     (load-param v0)
     (load-param v1)
     (goto :label)
     (const v0 0)
     (:label)
     (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(m3->get_code()),
            assembler::to_s_expr(expected_code3.get()));
}

// We had a bug where an invoke instruction inside an unreachable block of code
// would cause the whole IPCP domain to be set to bottom. This test checks that
// we handle it correctly.
TEST_F(InterproceduralConstantPropagationTest, unreachableInvoke) {
  Scope scope;
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto m1 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (const v0 0)
      (goto :skip)
      (invoke-static (v0) "LFoo;.qux:(I)V") ; this is unreachable
      (:skip)
      (invoke-static (v0) "LFoo;.baz:(I)V") ; this is reachable
      (return-void)
     )
    )
  )");
  m1->rstate.set_keep();
  creator.add_method(m1);

  auto m2 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.baz:(I)V"
     (
      (load-param v0)
      (return-void)
     )
    )
  )");
  creator.add_method(m2);

  auto m3 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.qux:(I)V"
      (
       (load-param v0)
       (return-void)
      )
    )
  )");
  creator.add_method(m3);

  auto cls = creator.create();
  scope.push_back(cls);

  call_graph::Graph cg(scope, /* include_virtuals */ false);
  walk::code(scope, [](DexMethod*, IRCode& code) { code.build_cfg(); });
  FixpointIterator fp_iter(cg);
  fp_iter.run({{CURRENT_PARTITION_LABEL, ArgumentDomain()}});

  // Check m2 is reachable, despite m3 being unreachable
  EXPECT_EQ(fp_iter.get_entry_state_at(m2).get(CURRENT_PARTITION_LABEL),
            ArgumentDomain({{0, SignedConstantDomain(0)}}));
  EXPECT_TRUE(fp_iter.get_entry_state_at(m3).is_bottom());
}

struct RuntimeAssertTest : public RedexTest {
  DexMethodRef* m_fail_handler;

  RuntimeAssertTest() {
    m_config.max_heap_analysis_iterations = 1;
    m_config.create_runtime_asserts = true;
    m_config.runtime_assert.param_assert_fail_handler = DexMethod::make_method(
        "Lcom/facebook/redex/"
        "ConstantPropagationAssertHandler;.paramValueError:(I)V");
    m_config.runtime_assert.field_assert_fail_handler = DexMethod::make_method(
        "Lcom/facebook/redex/"
        "ConstantPropagationAssertHandler;.fieldValueError:(Ljava/lang/"
        "String;)V");
    m_config.runtime_assert.return_value_assert_fail_handler =
        DexMethod::make_method(
            "Lcom/facebook/redex/"
            "ConstantPropagationAssertHandler;.returnValueError:(Ljava/lang/"
            "String;)V");
  }

  InterproceduralConstantPropagationPass::Config m_config;
};

TEST_F(RuntimeAssertTest, RuntimeAssertEquality) {
  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:(I)V"
     (
      (load-param v0)
      (return-void)
     )
    )
  )");

  ConstantEnvironment env{{0, SignedConstantDomain(5)}};
  RuntimeAssertTransform rat(m_config.runtime_assert);
  auto code = method->get_code();
  code->build_cfg();
  intraprocedural::FixpointIterator intra_cp(
      code->cfg(),
      [analyzer = ConstantPrimitiveAnalyzer()](auto* insn, auto* env) {
        analyzer.run(insn, env);
      });
  intra_cp.run(env);
  rat.apply(intra_cp, WholeProgramState(), method);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (load-param v0)
      (const v1 5)
      (if-eq v0 v1 :assertion-true)
      (const v2 0)
      (invoke-static (v2) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.paramValueError:(I)V")
      (:assertion-true)
      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(RuntimeAssertTest, RuntimeAssertSign) {
  using sign_domain::Interval;

  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:(II)V"
     (
      (load-param v0)
      (load-param v1)
      (return-void)
     )
    )
  )");

  ConstantEnvironment env{{0, SignedConstantDomain(Interval::GEZ)},
                          {1, SignedConstantDomain(Interval::LTZ)}};
  RuntimeAssertTransform rat(m_config.runtime_assert);
  auto code = method->get_code();
  code->build_cfg();
  intraprocedural::FixpointIterator intra_cp(
      code->cfg(),
      [analyzer = ConstantPrimitiveAnalyzer()](auto* insn, auto* env) {
        analyzer.run(insn, env);
      });
  intra_cp.run(env);
  rat.apply(intra_cp, WholeProgramState(), method);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (load-param v0)
      (load-param v1)
      (if-gez v0 :assertion-true-1)
      (const v2 0)
      (invoke-static (v2) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.paramValueError:(I)V")
      (:assertion-true-1)
      (if-ltz v1 :assertion-true-2)
      (const v3 1)
      (invoke-static (v3) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.paramValueError:(I)V")
      (:assertion-true-2)
      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(RuntimeAssertTest, RuntimeAssertCheckIntOnly) {
  using sign_domain::Interval;

  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:(JI)V"
     (
       (load-param v0) ; long -- we don't handle this yet
       (load-param v1) ; int
       (return-void)
     )
    )
  )");

  ConstantEnvironment env{{0, SignedConstantDomain(Interval::GEZ)},
                          {1, SignedConstantDomain(Interval::LTZ)}};
  RuntimeAssertTransform rat(m_config.runtime_assert);
  auto code = method->get_code();
  code->build_cfg();
  intraprocedural::FixpointIterator intra_cp(
      code->cfg(),
      [analyzer = ConstantPrimitiveAnalyzer()](auto* insn, auto* env) {
        analyzer.run(insn, env);
      });
  intra_cp.run(env);
  rat.apply(intra_cp, WholeProgramState(), method);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (load-param v0)
      (load-param v1)
      (if-ltz v1 :assertion-true-1)
      (const v2 1)
      (invoke-static (v2) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.paramValueError:(I)V")
      (:assertion-true-1)
      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(RuntimeAssertTest, RuntimeAssertCheckVirtualMethod) {
  using sign_domain::Interval;

  auto method = assembler::method_from_string(R"(
    (method (public) "LFoo;.bar:(I)V"
     (
      (load-param v0) ; `this` argument
      (load-param v1)
      (return-void)
     )
    )
  )");

  ConstantEnvironment env{{1, SignedConstantDomain(Interval::LTZ)}};
  RuntimeAssertTransform rat(m_config.runtime_assert);
  auto code = method->get_code();
  code->build_cfg();
  intraprocedural::FixpointIterator intra_cp(
      code->cfg(),
      [analyzer = ConstantPrimitiveAnalyzer()](auto* insn, auto* env) {
        analyzer.run(insn, env);
      });
  intra_cp.run(env);
  rat.apply(intra_cp, WholeProgramState(), method);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (load-param v0) ; `this` argument
      (load-param v1)
      (if-ltz v1 :assertion-true-1)
      (const v2 0)
      (invoke-static (v2) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.paramValueError:(I)V")
      (:assertion-true-1)
      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(RuntimeAssertTest, RuntimeAssertField) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  // We must create a field def and attach it to the DexClass instance (instead
  // of just creating an unattached field ref) so that when IPC calls
  // resolve_field() on Foo.qux, they will find it and treat it as a known field
  auto field = static_cast<DexField*>(DexField::make_field("LFoo;.qux:I"));
  field->make_concrete(ACC_PUBLIC | ACC_STATIC,
                       new DexEncodedValueBit(DEVT_INT, 1));
  creator.add_field(field);

  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (sget "LFoo;.qux:I")
      (move-result-pseudo v0)
      (return-void)
     )
    )
  )");
  creator.add_method(method);

  Scope scope{creator.create()};
  InterproceduralConstantPropagationPass(m_config).run(scope);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (sget "LFoo;.qux:I")
      (move-result-pseudo v0)
      (const v1 1)
      (if-eq v0 v1 :ok)

      (const-string "qux")
      (move-result-pseudo-object v2)
      (invoke-static (v2) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.fieldValueError:(Ljava/lang/String;)V")

      (:ok)
      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(RuntimeAssertTest, RuntimeAssertConstantReturnValue) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (invoke-static () "LFoo;.constantReturnValue:()I")
      (move-result v0)
      (return-void)
     )
    )
  )");
  creator.add_method(method);

  auto constant_return_method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.constantReturnValue:()I"
     (
      (const v0 1)
      (return v0)
     )
    )
  )");
  creator.add_method(constant_return_method);

  Scope scope{creator.create()};
  InterproceduralConstantPropagationPass(m_config).run(scope);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (invoke-static () "LFoo;.constantReturnValue:()I")
      (move-result v0)
      (const v1 1)
      (if-eq v0 v1 :ok)

      (const-string "constantReturnValue")
      (move-result-pseudo-object v2)
      (invoke-static (v2) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.returnValueError:(Ljava/lang/String;)V")

      (:ok)
      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(RuntimeAssertTest, RuntimeAssertNeverReturnsVoid) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (invoke-static () "LFoo;.neverReturns:()V")
      (return-void)
     )
    )
  )");
  creator.add_method(method);

  auto never_returns = assembler::method_from_string(R"(
    (method (public static) "LFoo;.neverReturns:()V"
     (
       (:loop)
       (goto :loop)
     )
    )
  )");
  creator.add_method(never_returns);

  Scope scope{creator.create()};
  InterproceduralConstantPropagationPass(m_config).run(scope);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (invoke-static () "LFoo;.neverReturns:()V")

      (const-string "neverReturns")
      (move-result-pseudo-object v0)
      (invoke-static (v0) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.returnValueError:(Ljava/lang/String;)V")

      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(RuntimeAssertTest, RuntimeAssertNeverReturnsConstant) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (invoke-static () "LFoo;.neverReturns:()I")
      (move-result v0)
      (return-void)
     )
    )
  )");
  creator.add_method(method);

  auto never_returns = assembler::method_from_string(R"(
    (method (public static) "LFoo;.neverReturns:()I"
     (
       (:loop)
       (goto :loop)
     )
    )
  )");
  creator.add_method(never_returns);

  Scope scope{creator.create()};
  InterproceduralConstantPropagationPass(m_config).run(scope);

  auto expected_code = assembler::ircode_from_string(R"(
    (
      (invoke-static () "LFoo;.neverReturns:()I")
      (move-result v0)

      (const-string "neverReturns")
      (move-result-pseudo-object v1)
      (invoke-static (v1) "Lcom/facebook/redex/ConstantPropagationAssertHandler;.returnValueError:(Ljava/lang/String;)V")

      (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(InterproceduralConstantPropagationTest, nonConstantField) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto field = static_cast<DexField*>(DexField::make_field("LFoo;.qux:I"));
  field->make_concrete(ACC_PUBLIC | ACC_STATIC,
                       new DexEncodedValueBit(DEVT_INT, 1));
  creator.add_field(field);

  auto m1 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (const v0 1)
      (sput v0 "LFoo;.qux:I")
      (return-void)
     )
    )
  )");
  m1->rstate.set_keep(); // Make this an entry point
  creator.add_method(m1);

  auto m2 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.baz:()V"
     (
      (sget "LFoo;.qux:I")
      (move-result-pseudo v0)
      (if-nez v0 :label)
      (const v0 0)
      (:label)
      (return-void)
     )
    )
  )");
  m2->rstate.set_keep(); // Make this an entry point
  creator.add_method(m2);

  Scope scope{creator.create()};
  walk::code(scope, [](DexMethod*, IRCode& code) { code.build_cfg(); });

  InterproceduralConstantPropagationPass::Config config;
  config.max_heap_analysis_iterations = 1;
  InterproceduralConstantPropagationPass(config).run(scope);

  auto expected_code2 = assembler::ircode_from_string(R"(
    (
     (const v0 1)
     (goto :label)
     (const v0 0)
     (:label)
     (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(m2->get_code()),
            assembler::to_s_expr(expected_code2.get()));
}

TEST_F(InterproceduralConstantPropagationTest, constantField) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto field = static_cast<DexField*>(DexField::make_field("LFoo;.qux:I"));
  field->make_concrete(ACC_PUBLIC | ACC_STATIC,
                       new DexEncodedValueBit(DEVT_INT, 1));
  creator.add_field(field);

  auto m1 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (const v0 0) ; this differs from the original encoded value of Foo.qux
      (sput v0 "LFoo;.qux:I")
      (return-void)
     )
    )
  )");
  m1->rstate.set_keep(); // Make this an entry point
  creator.add_method(m1);

  auto m2 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.baz:()V"
     (
      (sget "LFoo;.qux:I")
      (move-result-pseudo v0)
      (if-nez v0 :label)
      (const v0 0)
      (:label)
      (return-void)
     )
    )
  )");
  m2->rstate.set_keep(); // Make this an entry point
  creator.add_method(m2);

  Scope scope{creator.create()};
  walk::code(scope, [](DexMethod*, IRCode& code) { code.build_cfg(); });

  InterproceduralConstantPropagationPass::Config config;
  config.max_heap_analysis_iterations = 1;
  InterproceduralConstantPropagationPass(config).run(scope);

  auto expected_code2 = assembler::ircode_from_string(R"(
    (
     (sget "LFoo;.qux:I")
     (move-result-pseudo v0)
     (if-nez v0 :label)
     (const v0 0)
     (:label)
     (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(m2->get_code()),
            assembler::to_s_expr(expected_code2.get()));
}

TEST_F(InterproceduralConstantPropagationTest, constantFieldAfterClinit) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto field_qux = static_cast<DexField*>(DexField::make_field("LFoo;.qux:I"));
  field_qux->make_concrete(ACC_PUBLIC | ACC_STATIC,
                           new DexEncodedValueBit(DEVT_INT, 1));
  creator.add_field(field_qux);

  auto field_corge =
      static_cast<DexField*>(DexField::make_field("LFoo;.corge:I"));
  field_corge->make_concrete(ACC_PUBLIC | ACC_STATIC);
  creator.add_field(field_corge);

  auto clinit = assembler::method_from_string(R"(
    (method (public static) "LFoo;.<clinit>:()V"
     (
      (sget "LFoo;.qux:I")     ; Foo.qux is the constant 0 outside this clinit,
      (move-result-pseudo v0)  ; but we should check that we don't overwrite
      (sput v0 "LFoo;.corge:I") ; its initial encoded value while transforming
                               ; the clinit. I.e. this sget should be converted
                               ; to "const v0 1", not "const v0 0".

      (const v0 0) ; this differs from the original encoded value of Foo.qux,
                   ; but will be the only field value visible to other methods
      (sput v0 "LFoo;.qux:I")
      (return-void)
     )
    )
  )");
  clinit->rstate.set_keep(); // Make this an entry point
  creator.add_method(clinit);

  auto m = assembler::method_from_string(R"(
    (method (public static) "LFoo;.baz:()V"
     (
      (sget "LFoo;.qux:I")
      (move-result-pseudo v0) ; this is always zero due to <clinit>
      (if-nez v0 :label)
      (const v0 1)
      (:label)
      (return-void)
     )
    )
  )");
  m->rstate.set_keep(); // Make this an entry point
  creator.add_method(m);

  Scope scope{creator.create()};
  walk::code(scope, [](DexMethod*, IRCode& code) {
    code.build_cfg();
    code.cfg().calculate_exit_block();
  });

  InterproceduralConstantPropagationPass::Config config;
  config.max_heap_analysis_iterations = 2;

  auto fp_iter = InterproceduralConstantPropagationPass(config).analyze(scope);
  auto& wps = fp_iter->get_whole_program_state();
  EXPECT_EQ(wps.get_field_value(field_qux), SignedConstantDomain(0));
  EXPECT_EQ(wps.get_field_value(field_corge), SignedConstantDomain(1));

  InterproceduralConstantPropagationPass(config).run(scope);

  auto expected_clinit_code = assembler::ircode_from_string(R"(
     (
      (const v0 1)
      (const v0 0)
      (return-void)
     )
  )");

  EXPECT_EQ(assembler::to_s_expr(clinit->get_code()),
            assembler::to_s_expr(expected_clinit_code.get()));

  auto expected_code = assembler::ircode_from_string(R"(
    (
     (const v0 0)
     (const v0 1)
     (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(m->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(InterproceduralConstantPropagationTest,
       nonConstantFieldDueToInvokeInClinit) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto field_qux = static_cast<DexField*>(DexField::make_field("LFoo;.qux:I"));
  field_qux->make_concrete(ACC_PUBLIC | ACC_STATIC,
                           new DexEncodedValueBit(DEVT_INT, 0));
  creator.add_field(field_qux);

  auto clinit = assembler::method_from_string(R"(
    (method (public static) "LFoo;.<clinit>:()V"
     (
      (invoke-static () "LFoo;.initQux:()V")
      (return-void)
     )
    )
  )");
  clinit->rstate.set_keep(); // Make this an entry point
  creator.add_method(clinit);

  auto init_qux = assembler::method_from_string(R"(
    (method (public static) "LFoo;.initQux:()V"
     (
      (const v0 1) ; this differs from the original encoded value of Foo.qux
      (sput v0 "LFoo;.qux:I")
      (return-void)
     )
    )
  )");
  creator.add_method(init_qux);

  auto m = assembler::method_from_string(R"(
    (method (public static) "LFoo;.baz:()V"
     (
      (sget "LFoo;.qux:I")
      (move-result-pseudo v0)
      (if-nez v0 :label)
      (const v0 1)
      (:label)
      (return-void)
     )
    )
  )");
  m->rstate.set_keep(); // Make this an entry point
  creator.add_method(m);

  // We expect Foo.baz() to be unchanged since Foo.qux is not a constant
  auto expected = assembler::to_s_expr(m->get_code());

  Scope scope{creator.create()};
  walk::code(scope, [](DexMethod*, IRCode& code) {
    code.build_cfg();
    code.cfg().calculate_exit_block();
  });

  InterproceduralConstantPropagationPass::Config config;
  config.max_heap_analysis_iterations = 1;

  auto fp_iter = InterproceduralConstantPropagationPass(config).analyze(scope);
  auto& wps = fp_iter->get_whole_program_state();
  EXPECT_EQ(wps.get_field_value(field_qux), SignedConstantDomain::top());

  InterproceduralConstantPropagationPass(config).run(scope);
  EXPECT_EQ(assembler::to_s_expr(m->get_code()), expected);
}

TEST_F(InterproceduralConstantPropagationTest, constantReturnValue) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto m1 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:()V"
     (
      (invoke-static () "LFoo;.constantReturnValue:()I")
      (move-result v0)
      (if-eqz v0 :label)
      (const v0 1)
      (:label)
      (return-void)
     )
    )
  )");
  creator.add_method(m1);

  auto m2 = assembler::method_from_string(R"(
    (method (public static) "LFoo;.constantReturnValue:()I"
     (
      (const v0 0)
      (return v0)
     )
    )
  )");
  creator.add_method(m2);

  Scope scope{creator.create()};
  walk::code(scope, [](DexMethod*, IRCode& code) { code.build_cfg(); });

  InterproceduralConstantPropagationPass::Config config;
  config.max_heap_analysis_iterations = 1;
  InterproceduralConstantPropagationPass(config).run(scope);

  auto expected_code = assembler::ircode_from_string(R"(
    (
     (invoke-static () "LFoo;.constantReturnValue:()I")
     (move-result v0)
     (goto :label)
     (const v0 1)
     (:label)
     (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(m1->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(InterproceduralConstantPropagationTest, neverReturns) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto method = assembler::method_from_string(R"(
    (method (public static) "LFoo;.bar:(I)V"
     (
      (load-param v0)
      (if-eqz v0 :if-true-1)

      (invoke-static () "LFoo;.neverReturns:()V")
      (const v1 0) ; this never executes

      (:if-true-1)
      (const v1 1) ; this is the only instruction assigning to v1

      (const v2 1)
      (if-eq v1 v2 :if-true-2) ; this should always be true
      (const v3 2)
      (:if-true-2)
      (return-void)
     )
    )
  )");
  creator.add_method(method);
  method->rstate.set_keep(); // Make this an entry point

  auto never_returns = assembler::method_from_string(R"(
    (method (public static) "LFoo;.neverReturns:()V"
     (
       (:loop)
       (goto :loop)
     )
    )
  )");
  creator.add_method(never_returns);

  Scope scope{creator.create()};
  walk::code(scope, [](DexMethod*, IRCode& code) { code.build_cfg(); });

  InterproceduralConstantPropagationPass::Config config;
  config.max_heap_analysis_iterations = 1;
  InterproceduralConstantPropagationPass(config).run(scope);

  auto expected_code = assembler::ircode_from_string(R"(
    (
     (load-param v0)
     (if-eqz v0 :if-true-1)

     (invoke-static () "LFoo;.neverReturns:()V")
     (const v1 0)

     (:if-true-1)
     (const v1 1)

     (const v2 1)
     (goto :if-true-2)
     (const v3 2)
     (:if-true-2)
     (return-void)
    )
  )");

  EXPECT_EQ(assembler::to_s_expr(method->get_code()),
            assembler::to_s_expr(expected_code.get()));
}

TEST_F(InterproceduralConstantPropagationTest, whiteBoxReturnValues) {
  auto cls_ty = DexType::make_type("LFoo;");
  ClassCreator creator(cls_ty);
  creator.set_super(get_object_type());

  auto returns_void = assembler::method_from_string(R"(
    (method (public static) "LFoo;.returnsVoid:()V"
     (
      (return-void)
     )
    )
  )");
  creator.add_method(returns_void);

  auto never_returns = assembler::method_from_string(R"(
    (method (public static) "LFoo;.neverReturns:()V"
     (
       (:loop)
       (goto :loop)
     )
    )
  )");
  creator.add_method(never_returns);

  auto returns_constant = assembler::method_from_string(R"(
    (method (public static) "LFoo;.returnsConstant:()I"
     (
      (const v0 1)
      (return v0)
     )
    )
  )");
  creator.add_method(returns_constant);

  Scope scope{creator.create()};
  walk::code(scope, [](DexMethod*, IRCode& code) { code.build_cfg(); });

  InterproceduralConstantPropagationPass::Config config;
  config.max_heap_analysis_iterations = 1;
  auto fp_iter = InterproceduralConstantPropagationPass(config).analyze(scope);
  auto& wps = fp_iter->get_whole_program_state();

  // Make sure we mark methods that have a reachable return-void statement as
  // "returning" Top
  EXPECT_EQ(wps.get_return_value(returns_void), SignedConstantDomain::top());
  EXPECT_EQ(wps.get_return_value(never_returns),
            SignedConstantDomain::bottom());
  EXPECT_EQ(wps.get_return_value(returns_constant), SignedConstantDomain(1));
}
