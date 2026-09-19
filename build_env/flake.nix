{
  description = "A Nix-flake-based C/C++ development environment";
  nixConfig.bash-prompt-suffix = "(fnfelt) ";

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" ];
      forEachSupportedSystem = f: nixpkgs.lib.genAttrs supportedSystems (system: f {
        pkgs = import nixpkgs { inherit system; config.allowUnfree = true; };
      });
    in
    {
      devShells = forEachSupportedSystem ({ pkgs }: {
        default = pkgs.mkShell.override
          {
            # Override stdenv in order to change compiler:
             stdenv = pkgs.gcc16Stdenv;
          }
          {

            packages = with pkgs; [            
              cmake
              conan
              ninja
              ccache
              # For conan */system packages. Hint: nix-locate --whole-name dependency_name.pc | grep -v "^("
              pkg-config
              # Distributed build
              icecream
              # Lint CMake
              cmake-format
              # Lint/generate docstrings
              doxygen
              # For clang-format and clang-tidy
              clang-tools
              # Lint/format markdown (plugins combined into mdformat's Python env so
              # they are visible to the mdformat app; buildEnv gives a bin/ wrapper)
              (pkgs.buildEnv {
                name = "mdformat-env";
                paths = [
                  (python314.withPackages (ps: [
                    ps.mdformat
                    ps.mdformat-gfm
                    ps.mdformat-gfm-alerts
                    ps.mdformat-front-matters
                  ]))
                ];
              })
              # Lint commit messages
              gitlint
              # Simple Python based Google Style Guide linter
              cpplint

              # For agents:
              # JSON parser
              jq
              # html parser
              pup
              # YAML, JSON, INI and XML processor
              yq
              # GitHub CLI
              gh
              # Image manipulation (e.g. icon)
              imagemagick
            ];

            env = {
            };
          };
      });
    };
}