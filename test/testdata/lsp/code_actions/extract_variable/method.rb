# typed: true
# selective-apply-code-action: refactor.extract
# enable-experimental-lsp-extract-to-variable: true

def b; 1 + 123; end
#          ^^^ apply-code-action: [A] Extract Variable

def c; 1 + 1; 1 + 123; end
#                 ^^^ apply-code-action: [B] Extract Variable

def d
  1 + 123
#     ^^^ apply-code-action: [C] Extract Variable
end

def e
  1 + 1
  1 + 123
#     ^^^ apply-code-action: [D] Extract Variable
end

def f
  1 + 1
  1 + 2; 1 + 123
#            ^^^ apply-code-action: [E] Extract Variable
end
