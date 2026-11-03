/* pc_firewall.h - letting guests from other networks through the Windows firewall
 *
 * When Windows first asks about the game, its prompt allows it on private
 * networks only and adds a rule blocking it on public ones, which Windows
 * often takes a home connection for; a block rule wins over any allow
 * rule. Guests from another network then cannot reach a room hosted on
 * the computer (a guest's packets may come from another outside port than
 * the room has seen, which only an allow rule lets in). This asks for the
 * administrator's consent once (UAC) and, with Windows' own netsh, takes
 * away the game's rules and adds one allowing UDP to the game on every
 * network. On other systems there is nothing to do. */
#ifndef PC_FIREWALL_H
#define PC_FIREWALL_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_FIREWALL_NONE = -1, /* not Windows */
    PC_FIREWALL_IDLE = 0,
    PC_FIREWALL_RUNNING,   /* waiting for the consent and netsh */
    PC_FIREWALL_DONE,      /* the rule is there */
    PC_FIREWALL_DECLINED,  /* the consent was refused */
    PC_FIREWALL_FAILED,
};

/* Start it (on its own thread); does nothing while one is running */
void pc_firewall_allow_start(void);
int pc_firewall_state(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_FIREWALL_H */
