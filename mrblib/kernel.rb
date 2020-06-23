module Kernel
  def mrb_start(app)
    mrb_eval("Context.start('#{app.dup}', '#{Device.adapter}', '')", "#{app.dup}")
  end
end
