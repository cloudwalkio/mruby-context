##
# Kernel#mrb_eval

assert('Kernel#mrb_eval => true') do
  assert_true mrb_eval("a = 10")
end

assert('Kernel#mrb_eval => false') do
  assert_false mrb_eval("pasdfasdfasdff")
end

assert('Kernel#mrb_eval => false') do
  assert_raise(TypeError) do
    mrb_eval(10)
  end
end

assert('Kernel#mrb_eval Isolated') do
  $a = 10
  mrb_eval("$a = 10")
  assert_equal $a, 10
end

