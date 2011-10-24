struct port {
  GT_U8 ldev;
  GT_U8 lport;
};

extern struct port *ports;
extern int nports;

static inline int
port_valid (int n)
{
  return n >= 0 && n < nports;
}

extern int port_init (void);
extern GT_STATUS port_set_sgmii_mode (int);
extern int port_exists (GT_U8, GT_U8);
