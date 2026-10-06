# Runs before sway.fish (conf.d is sourced in name order, before config.fish), so sway and
# everything it launches (Pegasus, Utilities tools) also get ~/.local/bin (Claude Code).
contains $HOME/.local/bin $PATH; or set -gx PATH $HOME/.local/bin $PATH
