/* P1a spike — acados toolchain smoke runner.
 *
 * Links the code-generated OCP solver lib (libacados_ocp_solver_mass_spring.so,
 * built by `make ocp_shared_lib` from the gen_smoke.py output) plus libacados /
 * HPIPM / BLASFEO, sets the initial state, runs ONE SQP_RTI iteration, and
 * asserts the solver returns ACADOS_SUCCESS (status 0).
 *
 * This proves the full toolchain end-to-end:
 *   Python code-gen (Jinja2) -> C compile -> HPIPM RTI converge
 * with a new-ABI clean link (-D_GLIBCXX_USE_CXX11_ABI=1, see run_smoke.sh).
 *
 * API: the generated capsule exposes getters for the underlying ocp_nlp
 * objects (config/dims/in/out/opts). Setting x0 and reading the solution uses
 * the acados_c/ocp_nlp_interface.h accessors (ocp_nlp_out_get,
 * ocp_nlp_constraints_model_set), matching examples/c/getting_started.
 */
#include <stdio.h>
#include <stdlib.h>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_mass_spring.h"

#define NX 2
#define NU 1
#define N  20

int main(void) {
    int rc = 0;

    mass_spring_solver_capsule *capsule = mass_spring_acados_create_capsule();
    if (capsule == NULL) {
        fprintf(stderr, "SMOKE FAIL: create_capsule returned NULL\n");
        return 1;
    }

    if (mass_spring_acados_create(capsule) != 0) {
        fprintf(stderr, "SMOKE FAIL: acados_create failed\n");
        mass_spring_acados_free_capsule(capsule);
        return 1;
    }

    ocp_nlp_config *nlp_config = mass_spring_acados_get_nlp_config(capsule);
    ocp_nlp_dims    *nlp_dims  = mass_spring_acados_get_nlp_dims(capsule);
    ocp_nlp_out     *nlp_out   = mass_spring_acados_get_nlp_out(capsule);
    ocp_nlp_in      *nlp_in    = mass_spring_acados_get_nlp_in(capsule);

    /* Pin the initial state x0 = [p=1, v=0] by setting lbx == ubx at stage 0. */
    double x0[NX] = {1.0, 0.0};
    ocp_nlp_constraints_model_set(nlp_config, nlp_dims, nlp_in, 0, "lbx", x0);
    ocp_nlp_constraints_model_set(nlp_config, nlp_dims, nlp_in, 0, "ubx", x0);

    /* One SQP_RTI iteration. */
    int solver_status = mass_spring_acados_solve(capsule);

    /* Status of the last QP/RTI step lives in the solver object. */
    int acados_status = solver_status;
    printf("SMOKE: solver_status=%d\n", acados_status);

    if (acados_status != 0) {
        fprintf(stderr, "SMOKE FAIL: RTI did not converge (status=%d)\n", acados_status);
        rc = 1;
    }

    /* Sanity: read back stage-0 control and stage-1 state to prove a solve
     * actually produced a trajectory (not a silent no-op). */
    double u0[NU] = {0.0};
    double x1[NX] = {0.0, 0.0};
    ocp_nlp_out_get(nlp_config, nlp_dims, nlp_out, 0, "u", u0);
    ocp_nlp_out_get(nlp_config, nlp_dims, nlp_out, 1, "x", x1);
    printf("SMOKE: u[0]=%.4f  x[1]=[%.4f, %.4f]\n", u0[0], x1[0], x1[1]);

    mass_spring_acados_free(capsule);
    mass_spring_acados_free_capsule(capsule);

    if (rc == 0) {
        printf("SMOKE PASS: acados RTI converged via HPIPM\n");
    }
    return rc;
}
