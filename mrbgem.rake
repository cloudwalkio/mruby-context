MRuby::Gem::Specification.new('mruby-context') do |spec|
  spec.license = 'MIT'
  spec.authors = 'Thiago Scalone'

  spec.add_dependency('mruby-io')
  spec.add_dependency('mruby-require')
  spec.cc.include_paths << "#{build.root}/src"
end
