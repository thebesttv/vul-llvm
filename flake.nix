{
  description = "path-gen tool";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }: let
    system = "x86_64-linux";
    pkgs = import nixpkgs { inherit system; };
  in {
      packages."${system}".default =
        pkgs.stdenv.mkDerivation {
          pname = "path-gen";
          version = "0.0.1";

          sourceRoot = "llvm/llvm";
          srcs = [
            (pkgs.fetchFromGitHub {
              name = "llvm";
              owner = "thebesttv";
              repo = "vul-llvm";
              rev = "f5444894aeb89e610b2cfa558adcaf9eed7a819d";
              fetchSubmodules = false;
              sha256 = "sha256-/Frd4ei9VMl369vBe4m3QsQTF9S/a4FmM1xWinM2kGo=";
            })
            (pkgs.fetchFromGitHub {
              name = "fmt";
              owner = "fmtlib";
              repo = "fmt";
              rev = "11.0.2";
              fetchSubmodules = true;
              sha256 = "sha256-IKNt4xUoVi750zBti5iJJcCk3zivTt7nU12RIf8pM+0=";
            })
          ];

          enableParallelBuilding = true;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            python3
          ];

          cmakeFlags = [
            "-DFETCHCONTENT_SOURCE_DIR_FMT=../../../fmt"
            "-DLLVM_ENABLE_PROJECTS=clang"
            "-DCMAKE_BUILD_TYPE=Release"
          ];

        };
  };
}
