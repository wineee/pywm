{
  description = "pywm-atha - core of newm-atha";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
  flake-utils.lib.eachDefaultSystem (
    system:
    let
      pkgs = import nixpkgs {
        inherit system;
      };
      has_xwayland = true;
      inherit (pkgs.lib) optionals;
    in
    {
      packages.pywm-atha = (
        pkgs.python3.pkgs.buildPythonPackage rec {
          pname = "pywm-atha";
          version = "0.4alpha";

          # BEGIN f**king subprojects bug workaround for 'src = ./.'
          srcs = [
            ./.
          ];

          setSourceRoot = ''
            for i in ./*; do
              if [ -f "$i/pywm/pywm.py" ]; then
                sourceRoot=$i
              fi
            done
          '';

          preConfigure = ''
            echo "--- Pre-configure --------------"
            echo "  sourceRoot=$sourceRoot"
            echo "--- ls -------------------------"
            ls -al
            echo "--------------------------------"
          '';
          # END F**king subprojects bug workaround

          mesonFlags = if has_xwayland then [ "-Dxwayland=enabled" ] else [];

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
            glslang
          ];

          preBuild = "cd ..";

          buildInputs = with pkgs; [
            libGL
            wayland
            wayland-protocols
            libinput
            libxkbcommon
            pixman
            vulkan-loader
            mesa
            seatd

            libpng
            ffmpeg
            libcap

            xorg.xcbutilwm
            xorg.xcbutilrenderutil
            xorg.xcbutilerrors
            xorg.xcbutilimage
            xorg.libX11
          ] ++ optionals has_xwayland [
            xwayland
          ];

          propagatedBuildInputs = with pkgs.python3Packages; [
            imageio
            numpy
            pycairo
            evdev
          ];
        }
      );

      devShell = let
        my-python = pkgs.python3;
        python-with-my-packages = my-python.withPackages (ps: with ps; [
          imageio
          numpy
          pycairo
          evdev
          matplotlib

          python-lsp-server
          (pylsp-mypy.overrideAttrs (old: { pytestCheckPhase = "true"; }))
          mypy
        ]);
      in
        pkgs.mkShell {
          nativeBuildInputs = with pkgs; [ 
            meson
            ninja
            pkg-config
            wayland-scanner
            glslang
          ];

          buildInputs = with pkgs; [ 
            libGL
            libdrm
            wayland
            wayland-protocols
            libinput
            libxkbcommon
            pixman
            seatd
            vulkan-loader
            mesa

            libpng
            ffmpeg
            libcap
            python-with-my-packages 

            xorg.xcbutilwm
            xorg.xcbutilrenderutil
            xorg.xcbutilerrors
            xorg.xcbutilimage
            xorg.libX11
            xwayland
            wlroots_0_17
          ];

        };
    }
  );
}
