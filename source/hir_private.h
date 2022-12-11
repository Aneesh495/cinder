#ifndef CINDER_HIR_PRIVATE_H
#define CINDER_HIR_PRIVATE_H
#include "cinder.h"
CinderHIRExpr *cinder_hir_expression(CinderHIR *hir, const CinderExpr *source, bool value, unsigned depth, CinderDiagnostics *diags);
CinderHIRDecl *cinder_hir_declaration(CinderHIR *hir, const CinderDecl *source, unsigned depth, CinderDiagnostics *diags);
int cinder_hir_control_unit(CinderHIR *hir, CinderAst *ast, CinderDiagnostics *diags);
int cinder_verify_hir_control(const CinderHIR *hir, CinderDiagnostics *diags);
#endif
